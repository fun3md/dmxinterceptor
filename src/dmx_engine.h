#pragma once

#include <Arduino.h>
#include "config.h"

// ============================================================================
// DMX Engine - Handles DMX receive, merge, and transmit
// ============================================================================

class DMXEngine {
public:
    // Initialize both DMX ports
    void begin();

    // Call in loop or from FreeRTOS task to process DMX RX
    void rxTask();

    // Call in loop or from FreeRTOS task to process DMX TX
    void txTask();

    // Get pointer to input buffer (read-only, for web UI monitoring)
    const uint8_t* getInputBuffer() const { return _inputBuffer; }

    // Get pointer to output buffer (read-only, for web UI monitoring)
    const uint8_t* getOutputBuffer() const { return _outputBuffer; }

    // Get pointer to sACN override buffer (writable by sACN receiver)
    uint8_t* getSacnBuffer() { return _sacnBuffer; }

    // Get pointer to FX overlay buffer (writable by FX engine)
    uint8_t* getFxOverlay() { return _fxOverlay; }

    // Get pointer to FX mask (writable by FX engine)
    uint8_t* getFxMask() { return _fxMask; }

    // Set merge mode (MERGE_HTP or MERGE_LTP)
    void setMergeMode(uint8_t mode) { _mergeMode = mode; }
    uint8_t getMergeMode() const { return _mergeMode; }

    // Status
    bool isDmxInputActive() const { return _dmxInputActive; }
    unsigned long getLastRxTime() const { return _lastRxTime; }
    unsigned long getRxFps() const { return _rxFps; }
    unsigned long getTxFps() const { return _txFps; }
    unsigned long getRxPacketCount() const { return _rxPacketCount; }

private:
    // Perform channel merge: input + sACN + FX → output
    void mergeBuffers();

    // DMX data buffers (513 bytes each: start code + 512 channels)
    uint8_t _inputBuffer[DMX_PACKET_SIZE_FULL]  = {0};
    uint8_t _outputBuffer[DMX_PACKET_SIZE_FULL] = {0};
    uint8_t _sacnBuffer[DMX_CHANNELS]  = {0};  // 512 channels, no start code
    uint8_t _fxOverlay[DMX_CHANNELS]   = {0};  // FX values
    uint8_t _fxMask[DMX_CHANNELS]      = {0};  // 1 = FX controls this channel

    // Merge mode
    uint8_t _mergeMode = MERGE_HTP;

    // Status tracking
    volatile bool _dmxInputActive = false;
    volatile unsigned long _lastRxTime = 0;
    volatile unsigned long _rxPacketCount = 0;

    // FPS calculation
    unsigned long _rxFps = 0;
    unsigned long _txFps = 0;
    unsigned long _rxFrameCount = 0;
    unsigned long _txFrameCount = 0;
    unsigned long _lastFpsCalcTime = 0;

    // Mutex for buffer access
    SemaphoreHandle_t _bufferMutex = NULL;
};

// Global instance
extern DMXEngine dmxEngine;
