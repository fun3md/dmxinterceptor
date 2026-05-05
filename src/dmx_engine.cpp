#include "dmx_engine.h"
#include <esp_dmx.h>

// Global instance
DMXEngine dmxEngine;

void DMXEngine::begin() {
    Serial.println("[DMX] Initializing DMX Engine...");

    // Create buffer mutex for thread-safe access
    _bufferMutex = xSemaphoreCreateMutex();

    // ---------------------------------------------------------------
    // DMX INPUT (UART1) - Receive mode
    // ---------------------------------------------------------------
    dmx_config_t rxConfig = DMX_CONFIG_DEFAULT;
    dmx_personality_t rxPersonalities[] = {{DMX_CHANNELS, "RX"}};
    dmx_driver_install(DMX_INPUT_PORT, &rxConfig, rxPersonalities, 1);
    dmx_set_pin(DMX_INPUT_PORT, DMX_INPUT_TX_PIN, DMX_INPUT_RX_PIN, DMX_INPUT_EN_PIN);

    Serial.printf("[DMX] Input port %d configured: RX=%d, TX=%d, EN=%d\n",
                  DMX_INPUT_PORT, DMX_INPUT_RX_PIN, DMX_INPUT_TX_PIN, DMX_INPUT_EN_PIN);

    // ---------------------------------------------------------------
    // DMX OUTPUT (UART2) - Transmit mode
    // ---------------------------------------------------------------
    dmx_config_t txConfig = DMX_CONFIG_DEFAULT;
    dmx_personality_t txPersonalities[] = {{DMX_CHANNELS, "TX"}};
    dmx_driver_install(DMX_OUTPUT_PORT, &txConfig, txPersonalities, 1);
    dmx_set_pin(DMX_OUTPUT_PORT, DMX_OUTPUT_TX_PIN, DMX_OUTPUT_RX_PIN, DMX_OUTPUT_EN_PIN);

    Serial.printf("[DMX] Output port %d configured: TX=%d, RX=%d, EN=%d\n",
                  DMX_OUTPUT_PORT, DMX_OUTPUT_TX_PIN, DMX_OUTPUT_RX_PIN, DMX_OUTPUT_EN_PIN);

    _lastFpsCalcTime = millis();

    Serial.println("[DMX] DMX Engine initialized successfully");
}

void DMXEngine::rxTask() {
    // Wait for a DMX packet on the input port
    dmx_packet_t packet;
    int size = dmx_receive(DMX_INPUT_PORT, &packet, DMX_TIMEOUT_TICK);

    if (size > 0) {
        // Valid packet received
        if (!packet.err) {
            if (xSemaphoreTake(_bufferMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
                dmx_read(DMX_INPUT_PORT, _inputBuffer, size);
                xSemaphoreGive(_bufferMutex);
            }

            _dmxInputActive = true;
            _lastRxTime = millis();
            _rxPacketCount++;
            _rxFrameCount++;
        }
    } else {
        // Timeout - no DMX signal
        if (_dmxInputActive && (millis() - _lastRxTime > 1000)) {
            _dmxInputActive = false;
            Serial.println("[DMX] Input signal lost!");
        }
    }
}

void DMXEngine::txTask() {
    // Merge all sources into the output buffer
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
    if (xSemaphoreTake(_bufferMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        // Start code is always 0x00 for standard DMX
        _outputBuffer[0] = 0x00;

        // Merge channels 1-512 (buffer index 1-512)
        for (int i = 0; i < DMX_CHANNELS; i++) {
            uint8_t inputVal = _inputBuffer[i + 1];  // +1 to skip start code
            uint8_t sacnVal  = _sacnBuffer[i];
            uint8_t fxVal    = _fxOverlay[i];
            bool    fxActive = _fxMask[i] != 0;

            if (fxActive) {
                // FX takes full control of this channel
                _outputBuffer[i + 1] = fxVal;
            } else if (sacnVal > 0) {
                // sACN merge
                if (_mergeMode == MERGE_HTP) {
                    _outputBuffer[i + 1] = max(inputVal, sacnVal);
                } else {
                    // LTP: sACN overrides if non-zero
                    _outputBuffer[i + 1] = sacnVal;
                }
            } else {
                // Pure pass-through
                _outputBuffer[i + 1] = inputVal;
            }
        }

        xSemaphoreGive(_bufferMutex);
    }
}
