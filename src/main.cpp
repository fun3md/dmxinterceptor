// ============================================================================
// DMX Interceptor - ESP32 Firmware
// ============================================================================
// Transparent DMX pass-through with sACN override, FX macros, fixture learning
//
// Hardware: ESP-WROOM-32 + 2x MAX485 RS485 modules
//
// --------------------------------------------------------------------------
// Core pinning model
// --------------------------------------------------------------------------
//   Core 1  (real-time, no WiFi):
//     - DMX_INIT (one-shot)     : dmx_driver_install()
//     - DMX_RX   (priority 5)   : dmx_receive() -> _inputBuffer
//     - DMX_TX   (priority 5)   : mergeBuffers() -> dmx_send()
//
//   Core 0  (application / network):
//     - APP      (priority 1)   : WiFi, sACN, ArtNet, OSC, FX, learn, status
//     - Async TCP (system)      : ESPAsyncWebServer (CONFIG_ASYNC_TCP_RUNNING_CORE=0)
//     - WiFi stack (system)     : STA / AP, sACN/ArtNet/OSC UDP callbacks
//
// Why this matters:
//   The DMX UART driver is interrupt-driven.  The WiFi stack generates a
//   high rate of interrupts and does heavy memcpy work in its callbacks
//   (e.g. serving the 512-channel /api/dmx/* endpoints).  Running both on
//   the same core can starve the DMX ISRs and make dmx_receive() time out,
//   which is what produced the "DMX input signal lost" message as soon as
//   the web UI was opened.  Keeping DMX pinned to Core 1 isolates it
//   completely from WiFi activity.
// ============================================================================

#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "dmx_engine.h"
#include "sacn_receiver.h"
#include "web_server.h"
#include "fixture_manager.h"
#include "fx_engine.h"
#include "artnet_sender.h"
#include "osc_receiver.h"

// ============================================================================
// FreeRTOS Task Handles
// ============================================================================
TaskHandle_t dmxRxTaskHandle = NULL;
TaskHandle_t dmxTxTaskHandle = NULL;
static SemaphoreHandle_t _dmxInitDone = NULL;

// ============================================================================
// Core 1 — DMX Init (one-shot)
// Installs the DMX drivers on Core 1 so the UART ISRs land on a core with
// no WiFi or application load.
// ============================================================================
void dmxInitTaskFunc(void* parameter) {
    Serial.printf("[TASK] DMX INIT on core %d\n", xPortGetCoreID());
    dmxEngine.begin();
    xSemaphoreGive(_dmxInitDone);
    vTaskDelete(NULL);
}

// ============================================================================
// Core 1 — DMX RX (priority 5)
// Blocks in dmx_receive(); the esp_dmx ISR wakes it when a full frame
// arrives.  Nothing else runs on this core, so WiFi cannot delay it.
// ============================================================================
void dmxRxTaskFunc(void* parameter) {
    Serial.println("[TASK] DMX RX on core " + String(xPortGetCoreID()));
    for (;;) {
        dmxEngine.rxTask();
    }
}

// ============================================================================
// Core 1 — DMX TX (priority 5)
// Runs mergeBuffers() and dmx_send() at the DMX refresh rate.  Also
// pinned to Core 1 so the on-air DMX frame timing is never disturbed.
// ============================================================================
void dmxTxTaskFunc(void* parameter) {
    Serial.println("[TASK] DMX TX on core " + String(xPortGetCoreID()));
    for (;;) {
        dmxEngine.txTask();
    }
}

// ============================================================================
// Status Reporting (called from APP task on Core 0)
// ============================================================================
unsigned long lastStatusPrint = 0;

void printStatus() {
    unsigned long now = millis();
    if (now - lastStatusPrint < 10000) return;
    lastStatusPrint = now;

    Serial.println("--- DMX Interceptor ---");
    Serial.printf("  DMX: %s  RX:%lu TX:%lu fps\n",
                  dmxEngine.isDmxInputActive() ? "OK" : "NO SIG",
                  dmxEngine.getRxFps(), dmxEngine.getTxFps());
    Serial.printf("  sACN: %s  WiFi: %s %s\n",
                  sacnReceiver.isActive() ? "OK" : "--",
                  wifiManager.isAPMode() ? "AP" : "STA",
                  wifiManager.getIP().c_str());
    Serial.printf("  Fixtures: %d  Macros: %d  Heap: %d\n",
                  fixtureManager.getFixtureCount(),
                  fxEngine.getMacroCount(),
                  ESP.getFreeHeap());
    Serial.printf("  OSC: %s  port:%d  pkts:%lu\n",
                  oscReceiver.isEnabled() ? "ON" : "OFF",
                  oscReceiver.getPort(),
                  oscReceiver.getPacketCount());
    Serial.println("-----------------------");
}

// ============================================================================
// Core 0 — Application task (priority 1, low)
// Owns everything that is not time-critical DMX:
//   - WiFiManager.loop()       : STA reconnect / status
//   - sacnReceiver.loop()      : pull E1.31 packets from the async queue
//   - sacnReceiver → dmxEngine : copy sACN data into the merge buffer
//   - fxEngine.process()       : active macro effects → FX overlay + mask
//   - fixtureManager learn     : learn-mode probe writes
//   - oscReceiver.loop()       : TouchOSC messages → FX overlay
//   - artnetSender.send()      : forward DMX over Art-Net (rate-limited)
//   - status LED + printStatus
//   - ESPAsyncWebServer lives on the system async-TCP task (also Core 0)
// ============================================================================
void appTaskFunc(void* parameter) {
    Serial.println("[TASK] APP on core " + String(xPortGetCoreID()));
    for (;;) {
        // --- WiFi ---
        wifiManager.loop();

        // --- sACN ---
        sacnReceiver.loop();

        // Sync sACN data → DMX merge buffer (Core 0 → Core 1 shared memory).
        // Byte-atomic on ESP32, so a torn copy is impossible; the merge
        // task on Core 1 always sees a consistent buffer within one frame.
        memcpy(dmxEngine.getSacnBuffer(), sacnReceiver.getDataBuffer(), DMX_CHANNELS);

        // --- FX engine ---
        fxEngine.process(dmxEngine.getFxOverlay(), dmxEngine.getFxMask(),
                         sacnReceiver.getTriggerBuffer());

        // --- Fixture learn mode ---
        if (fixtureManager.getLearnState() == LEARN_SCANNING) {
            fixtureManager.learnScanNext(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());
        } else if (fixtureManager.getLearnState() == LEARN_PROBING_DIMMER ||
                   fixtureManager.getLearnState() == LEARN_PROBING_RGB) {
            fixtureManager.learnProbe(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());
        }

        // --- OSC ---
        oscReceiver.loop();
        oscReceiver.applyToFxOverlay(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());

        // --- ArtNet output ---
        if (artnetSender.isEnabled()) {
            if (artnetSender.getSource() == 0) {
                // Source: raw DMX input — send only when a new frame has arrived.
                // Use a local snapshot so we never read the live input buffer
                // without holding the engine mutex.
                static unsigned long lastArtNetRxCount = 0;
                static uint8_t       snapshotBuf[DMX_CHANNELS];
                unsigned long curRxCount = dmxEngine.getRxPacketCount();
                if (curRxCount != lastArtNetRxCount) {
                    lastArtNetRxCount = curRxCount;
                    dmxEngine.copyInputBuffer(snapshotBuf, DMX_PACKET_SIZE_FULL);
                    artnetSender.send(snapshotBuf + 1, DMX_CHANNELS);
                }
            } else {
                // Source: merged output — rate-limit to ~40 fps.
                static unsigned long lastArtNetTx = 0;
                static uint8_t       snapshotBuf[DMX_CHANNELS];
                unsigned long now = millis();
                if (now - lastArtNetTx >= 25) {
                    lastArtNetTx = now;
                    dmxEngine.copyOutputBuffer(snapshotBuf, DMX_PACKET_SIZE_FULL);
                    artnetSender.send(snapshotBuf + 1, DMX_CHANNELS);
                }
            }
        }

        // --- Status LED ---
        static unsigned long lastBlink = 0;
        unsigned long now = millis();
        unsigned long rate = dmxEngine.isDmxInputActive() ? 250 : 1000;
        if (now - lastBlink >= rate) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            lastBlink = now;
        }

        printStatus();
        delay(1);
    }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("============================================");
    Serial.println("  DMX Interceptor v1.0");
    Serial.println("  ESP-WROOM-32 | Full Feature Set");
    Serial.println("============================================\n");

    // Status LED
    pinMode(STATUS_LED_PIN, OUTPUT);

    // 1. Config from LittleFS
    configManager.begin();

    // 2. WiFi (managed by APP task on Core 0)
    wifiManager.setCredentials(
        configManager.config().wifiSSID,
        configManager.config().wifiPassword
    );
    wifiManager.begin();

    // 3. sACN (looped by APP task on Core 0)
    sacnReceiver.setDataUniverse(configManager.config().sacnDataUniverse);
    sacnReceiver.setTriggerUniverse(configManager.config().sacnTriggerUniverse);
    sacnReceiver.begin();

    // 4. Fixtures & FX (looped by APP task on Core 0)
    fixtureManager.begin();
    fxEngine.begin();

    // 5. DMX engine — initialise on Core 1 so the UART ISRs are far away
    //    from the WiFi interrupt load on Core 0.
    dmxEngine.setMergeMode(configManager.config().mergeMode);
    _dmxInitDone = xSemaphoreCreateBinary();
    TaskHandle_t dmxInitHandle = NULL;
    xTaskCreatePinnedToCore(dmxInitTaskFunc, "DMX_INIT", 4096,
                            NULL, configMAX_PRIORITIES - 1, &dmxInitHandle, DMX_CORE);
    xSemaphoreTake(_dmxInitDone, portMAX_DELAY);
    vSemaphoreDelete(_dmxInitDone);
    _dmxInitDone = NULL;

    // 6. ArtNet sender (triggered from APP task on Core 0)
    artnetSender.setEnabled(configManager.config().artnetEnabled);
    artnetSender.setTargetIP(configManager.config().artnetTargetIP);
    artnetSender.setUniverse(configManager.config().artnetUniverse);
    artnetSender.setSource(configManager.config().artnetSource);
    artnetSender.begin();

    // 7. OSC receiver (looped by APP task on Core 0)
    oscReceiver.setEnabled(configManager.config().oscEnabled);
    oscReceiver.setPort(configManager.config().oscPort);
    oscReceiver.setFeedbackEnabled(configManager.config().oscFeedbackEnabled);
    oscReceiver.setFeedbackPort(configManager.config().oscFeedbackPort);
    oscReceiver.begin();

    // 8. Web server (served by the system async-TCP task on Core 0).
    //    API handlers run on Core 0 and call into dmxEngine via the
    //    copyInputBuffer / copyOutputBuffer helpers for safe cross-core access.
    webServer.begin();

    Serial.printf("[MAIN] Web UI: http://%s/\n\n", wifiManager.getIP().c_str());

    // 9. DMX tasks — Core 1, high priority, real-time
    xTaskCreatePinnedToCore(dmxRxTaskFunc, "DMX_RX", DMX_RX_STACK_SIZE,
                            NULL, DMX_RX_TASK_PRIORITY, &dmxRxTaskHandle, DMX_CORE);
    xTaskCreatePinnedToCore(dmxTxTaskFunc, "DMX_TX", DMX_TX_STACK_SIZE,
                            NULL, DMX_TX_TASK_PRIORITY, &dmxTxTaskHandle, DMX_CORE);

    // 10. Application task — Core 0, low priority
    xTaskCreatePinnedToCore(appTaskFunc, "APP", 8192,
                            NULL, 1, NULL, WIFI_CORE);

    Serial.println("[MAIN] All systems go.\n");
}

// ============================================================================
// Loop - runs on Core 1 (Arduino default).  All real work is in the pinned
// tasks above, so the Arduino task is no longer needed.
// ============================================================================
void loop() {
    vTaskDelete(NULL);
}
