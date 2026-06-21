#include "dmx_engine.h"
#include <esp_dmx.h>

// Global instance
DMXEngine dmxEngine;

void DMXEngine::begin() {
    // Guard against double-init: dmx_driver_install() crashes the ESP if called
    // a second time for the same port (the library explicitly rejects it).
    if (dmx_driver_is_installed(DMX_INPUT_PORT) ||
        dmx_driver_is_installed(DMX_OUTPUT_PORT)) {
        Serial.println("[DMX] Already initialized, skipping.");
        return;
    }

    Serial.println("[DMX] Initializing DMX Engine...");

    // Create buffer mutex for thread-safe access
    _bufferMutex = xSemaphoreCreateMutex();

    // ---------------------------------------------------------------
    // DMX INPUT (UART1) - Receive mode
    // ---------------------------------------------------------------
    Serial.println("[DMX] Installing input driver..."); Serial.flush();
    dmx_config_t rxConfig = DMX_CONFIG_DEFAULT;
    dmx_personality_t rxPersonalities[] = {{DMX_CHANNELS, "RX"}};
    bool rxOk = dmx_driver_install(DMX_INPUT_PORT, &rxConfig, rxPersonalities, 1);
    Serial.printf("[DMX] Input driver install: %s\n", rxOk ? "OK" : "FAIL"); Serial.flush();

    bool rxPinOk = dmx_set_pin(DMX_INPUT_PORT, DMX_INPUT_TX_PIN, DMX_INPUT_RX_PIN, DMX_INPUT_EN_PIN);
    Serial.printf("[DMX] Input port %d pin set: %s (RX=%d, TX=%d, EN=%d)\n",
                  DMX_INPUT_PORT, rxPinOk ? "OK" : "FAIL",
                  DMX_INPUT_RX_PIN, DMX_INPUT_TX_PIN, DMX_INPUT_EN_PIN);
    Serial.flush();

    // ---------------------------------------------------------------
    // DMX OUTPUT (UART2) - Transmit mode
    // ---------------------------------------------------------------
    Serial.printf("[DMX] Installing output driver on port %d...\n", DMX_OUTPUT_PORT); Serial.flush();
    dmx_config_t txConfig = DMX_CONFIG_DEFAULT;
    dmx_personality_t txPersonalities[] = {{DMX_CHANNELS, "TX"}};
    bool txOk = dmx_driver_install(DMX_OUTPUT_PORT, &txConfig, txPersonalities, 1);
    Serial.printf("[DMX] Output driver install: %s\n", txOk ? "OK" : "FAIL"); Serial.flush();

    bool txPinOk = dmx_set_pin(DMX_OUTPUT_PORT, DMX_OUTPUT_TX_PIN, DMX_OUTPUT_RX_PIN, DMX_OUTPUT_EN_PIN);
    Serial.printf("[DMX] Output port %d pin set: %s (TX=%d, RX=%d, EN=%d)\n",
                  DMX_OUTPUT_PORT, txPinOk ? "OK" : "FAIL",
                  DMX_OUTPUT_TX_PIN, DMX_OUTPUT_RX_PIN, DMX_OUTPUT_EN_PIN);
    Serial.flush();

    _lastFpsCalcTime = millis();

    Serial.println("[DMX] DMX Engine initialized successfully");
}

void DMXEngine::rxTask() {
    // Wait for a DMX packet on the input port.
    // DMX_TIMEOUT_TICK = 1250 ms (per DMX spec).
    dmx_packet_t packet;
    int size = dmx_receive(DMX_INPUT_PORT, &packet, DMX_TIMEOUT_TICK);

    if (size > 0) {
        // Valid packet received
        if (!packet.err) {
            // Hold the mutex while dmx_read() copies into _inputBuffer so the
            // web server (Core 0) never sees a half-updated buffer.
            if (xSemaphoreTake(_bufferMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                dmx_read(DMX_INPUT_PORT, _inputBuffer, size);
                xSemaphoreGive(_bufferMutex);
            }

            _dmxInputActive = true;
            _lastRxTime = millis();
            _rxPacketCount++;
            _rxFrameCount++;
        }
    } else {
        // dmx_receive() timed out: no complete DMX frame in the last ~1.25 s.
        if (_dmxInputActive && (millis() - _lastRxTime > 1000)) {
            _dmxInputActive = false;
            Serial.println("[DMX] Input signal lost!");
        }
    }
}

void DMXEngine::txTask() {
    // Merge all sources into the output buffer (always, even under contention).
    mergeBuffers();

    // Write merged data and send
    dmx_write(DMX_OUTPUT_PORT, _outputBuffer, DMX_PACKET_SIZE_FULL);
    dmx_send(DMX_OUTPUT_PORT);

    _txFrameCount++;

    // Wait for TX to complete before sending next frame
    dmx_wait_sent(DMX_OUTPUT_PORT, DMX_TIMEOUT_TICK);

    // Calculate FPS every second
    unsigned long now = millis();
    if (now - _lastFpsCalcTime >= 1000) {
        _rxFps = _rxFrameCount;
        _txFps = _txFrameCount;
        _rxFrameCount = 0;
        _txFrameCount = 0;
        _lastFpsCalcTime = now;
    }
}

void DMXEngine::mergeBuffers() {
    // Take a short-lived snapshot of all source buffers under the mutex,
    // then build _outputBuffer from the local copy.  This guarantees the
    // output is refreshed EVERY frame — the previous version could silently
    // drop the merge when the mutex was contended, leaving stale DMX on
    // the output (which is what was producing the "frozen output" look
    // when the web interface was hammering the API).
    uint8_t localInput[DMX_PACKET_SIZE_FULL];
    uint8_t localSacn[DMX_CHANNELS];
    uint8_t localFxOverlay[DMX_CHANNELS];
    uint8_t localFxMask[DMX_CHANNELS];

    if (xSemaphoreTake(_bufferMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        memcpy(localInput,     _inputBuffer, DMX_PACKET_SIZE_FULL);
        memcpy(localSacn,      _sacnBuffer,  DMX_CHANNELS);
        memcpy(localFxOverlay, _fxOverlay,   DMX_CHANNELS);
        memcpy(localFxMask,    _fxMask,      DMX_CHANNELS);
        xSemaphoreGive(_bufferMutex);
    } else {
        // Mutex contended — fall back to a lock-free copy.  Byte-atomic on
        // ESP32; worst case is one frame with a torn read (the next frame
        // is consistent).
        memcpy(localInput,     _inputBuffer, DMX_PACKET_SIZE_FULL);
        memcpy(localSacn,      _sacnBuffer,  DMX_CHANNELS);
        memcpy(localFxOverlay, _fxOverlay,   DMX_CHANNELS);
        memcpy(localFxMask,    _fxMask,      DMX_CHANNELS);
    }

    // Start code is always 0x00 for standard DMX
    localInput[0] = 0x00;

    // Merge channels 1-512 (buffer index 1-512)
    for (int i = 0; i < DMX_CHANNELS; i++) {
        uint8_t inputVal = localInput[i + 1];   // +1 to skip start code
        uint8_t sacnVal  = localSacn[i];
        uint8_t fxVal    = localFxOverlay[i];
        bool    fxActive = localFxMask[i] != 0;

        if (fxActive) {
            // FX takes full control of this channel
            localInput[i + 1] = fxVal;
        } else if (sacnVal > 0) {
            // sACN merge
            if (_mergeMode == MERGE_HTP) {
                localInput[i + 1] = max(inputVal, sacnVal);
            } else {
                // LTP: sACN overrides if non-zero
                localInput[i + 1] = sacnVal;
            }
        } else {
            // Pure pass-through
            localInput[i + 1] = inputVal;
        }
    }

    // Publish the merged frame.  _outputBuffer is only ever written from
    // this Core-1 task, so no extra lock is required.
    memcpy(_outputBuffer, localInput, DMX_PACKET_SIZE_FULL);
}

void DMXEngine::copyInputBuffer(uint8_t* dst, size_t len) const {
    if (!dst || len == 0) return;
    size_t n = (len > DMX_PACKET_SIZE_FULL) ? DMX_PACKET_SIZE_FULL : len;
    if (xSemaphoreTake(_bufferMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        memcpy(dst, _inputBuffer, n);
        xSemaphoreGive(_bufferMutex);
    } else {
        memcpy(dst, _inputBuffer, n);
    }
}

void DMXEngine::copyOutputBuffer(uint8_t* dst, size_t len) const {
    if (!dst || len == 0) return;
    size_t n = (len > DMX_PACKET_SIZE_FULL) ? DMX_PACKET_SIZE_FULL : len;
    // _outputBuffer is only ever written from txTask() on Core 1.
    // A lock-free copy is safe because we always copy the full frame in
    // one go in mergeBuffers(); the only risk is reading a mid-merge value
    // which is a single frame at most.
    memcpy(dst, _outputBuffer, n);
}
