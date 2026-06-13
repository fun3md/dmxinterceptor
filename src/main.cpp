// ============================================================================
// DMX Interceptor - ESP32 Firmware
// ============================================================================
// Transparent DMX pass-through with sACN override, FX macros, fixture learning
//
// Hardware: ESP-WROOM-32 + 2x MAX485 RS485 modules
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

// ============================================================================
// FreeRTOS Task Handles
// ============================================================================
TaskHandle_t dmxRxTaskHandle = NULL;
TaskHandle_t dmxTxTaskHandle = NULL;

// ============================================================================
// FreeRTOS Tasks - pinned to Core 1 for real-time performance
// ============================================================================
void dmxRxTaskFunc(void* parameter) {
    Serial.println("[TASK] DMX RX on core " + String(xPortGetCoreID()));
    for (;;) {
        dmxEngine.rxTask();
    }
}

void dmxTxTaskFunc(void* parameter) {
    Serial.println("[TASK] DMX TX on core " + String(xPortGetCoreID()));
    for (;;) {
        dmxEngine.txTask();
    }
}

// ============================================================================
// Status Reporting
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
    Serial.println("-----------------------");
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

    // 2. WiFi
    wifiManager.setCredentials(
        configManager.config().wifiSSID,
        configManager.config().wifiPassword
    );
    wifiManager.begin();

    // 3. sACN
    sacnReceiver.setDataUniverse(configManager.config().sacnDataUniverse);
    sacnReceiver.setTriggerUniverse(configManager.config().sacnTriggerUniverse);
    sacnReceiver.begin();

    // 4. Fixtures & FX
    fixtureManager.begin();
    fxEngine.begin();

    // 5. DMX engine
    dmxEngine.setMergeMode(configManager.config().mergeMode);
    dmxEngine.begin();

    // 6. ArtNet sender (after WiFi is up)
    artnetSender.setEnabled(configManager.config().artnetEnabled);
    artnetSender.setTargetIP(configManager.config().artnetTargetIP);
    artnetSender.setUniverse(configManager.config().artnetUniverse);
    artnetSender.setSource(configManager.config().artnetSource);
    artnetSender.begin();

    // 7. Web server
    webServer.begin();

    Serial.printf("[MAIN] Web UI: http://%s/\n\n", wifiManager.getIP().c_str());

    // 7. DMX tasks on Core 1
    xTaskCreatePinnedToCore(dmxRxTaskFunc, "DMX_RX", DMX_RX_STACK_SIZE,
                            NULL, DMX_RX_TASK_PRIORITY, &dmxRxTaskHandle, DMX_CORE);
    xTaskCreatePinnedToCore(dmxTxTaskFunc, "DMX_TX", DMX_TX_STACK_SIZE,
                            NULL, DMX_TX_TASK_PRIORITY, &dmxTxTaskHandle, DMX_CORE);

    Serial.println("[MAIN] All systems go.\n");
}

// ============================================================================
// Loop - Core 0: WiFi, sACN, FX processing, status
// ============================================================================
void loop() {
    wifiManager.loop();
    sacnReceiver.loop();

    // Sync sACN data → DMX merge buffer
    memcpy(dmxEngine.getSacnBuffer(), sacnReceiver.getDataBuffer(), DMX_CHANNELS);

    // Process FX macros → FX overlay + mask
    fxEngine.process(dmxEngine.getFxOverlay(), dmxEngine.getFxMask(),
                     sacnReceiver.getTriggerBuffer());

    // Learn mode (if active, overrides FX overlay)
    if (fixtureManager.getLearnState() == LEARN_SCANNING) {
        fixtureManager.learnScanNext(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());
    } else if (fixtureManager.getLearnState() == LEARN_PROBING_DIMMER ||
               fixtureManager.getLearnState() == LEARN_PROBING_RGB) {
        fixtureManager.learnProbe(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());
    }

    // --- ArtNet output (runs on Core 0 alongside WiFi stack) ---
    if (artnetSender.isEnabled()) {
        if (artnetSender.getSource() == 0) {
            // Source: raw DMX input — send only when a new frame has arrived
            static unsigned long lastArtNetRxCount = 0;
            unsigned long curRxCount = dmxEngine.getRxPacketCount();
            if (curRxCount != lastArtNetRxCount) {
                lastArtNetRxCount = curRxCount;
                artnetSender.send(dmxEngine.getInputBuffer() + 1, DMX_CHANNELS);
            }
        } else {
            // Source: merged output — rate-limit to ~40 fps
            static unsigned long lastArtNetTx = 0;
            unsigned long now = millis();
            if (now - lastArtNetTx >= 25) {
                lastArtNetTx = now;
                artnetSender.send(dmxEngine.getOutputBuffer() + 1, DMX_CHANNELS);
            }
        }
    }

    // Status LED
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
