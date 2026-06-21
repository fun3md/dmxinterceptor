#pragma once

#include <Arduino.h>
#include "config.h"
#include "artnet_sender.h"

// ============================================================================
// DMX Engine - Handles DMX receive, merge, and transmit
// ============================================================================

// ============================================================================
// Threading model
// ----------------------------------------------------------------------------
// Core 1 (real-time):
//   - rxTask()      : writes _inputBuffer via dmx_read()
//   - txTask()      : calls mergeBuffers() -> writes _outputBuffer
//   - mergeBuffers(): reads _inputBuffer, _sacnBuffer, _fxOverlay, _fxMask
//
// Core 0 (application / network):
//   - APP task      : writes _sacnBuffer, _fxOverlay, _fxMask (FX, sACN, OSC)
//   - web server    : reads _inputBuffer, _outputBuffer (for /api/dmx/*)
//
// Cross-core safety:
//   - rxTask() holds _bufferMutex during dmx_read() so web-server reads
//     never observe a half-updated _inputBuffer.
//   - mergeBuffers() takes a short-lived snapshot under the same mutex so
//     the merge NEVER silently skips a frame (previous behavior could leave
//     the DMX output frozen if the mutex was contended by the web server).
//   - APP-task writes to _sacnBuffer / _fxOverlay / _fxMask are byte-atomic
//     on ESP32; mergeBuffers() tolerates a briefly mixed snapshot (one stale
//     frame is acceptable, the next frame is consistent).
// ============================================================================

class DMXEngine {
public:
    // Initialize both DMX ports
    void begin();

    // Call from the pinned Core 1 task to process DMX RX
    void rxTask();

    // Call from the pinned Core 1 task to process DMX TX
    void txTask();

    // ---- Buffer access (raw pointers, lock-free; safe for byte-wise reads) ----
    const uint8_t* getInputBuffer()  const { return _inputBuffer;  }
    const uint8_t* getOutputBuffer() const { return _outputBuffer; }
    uint8_t* getSacnBuffer()               { return _sacnBuffer;    }
    uint8_t* getFxOverlay()                { return _fxOverlay;     }
    uint8_t* getFxMask()                   { return _fxMask;        }

    // ---- Thread-safe buffer snapshots (use from Core 0 / web server) ----
    // Copy the input buffer into the caller-provided array (must be 513 bytes).
    void copyInputBuffer(uint8_t* dst, size_t len) const;
    // Copy the output buffer into the caller-provided array (must be 513 bytes).
    void copyOutputBuffer(uint8_t* dst, size_t len) const;

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

    // Mutex for buffer access (mutable so const readers can lock it)
    mutable SemaphoreHandle_t _bufferMutex = NULL;
};

// Global instance
extern DMXEngine dmxEngine;
