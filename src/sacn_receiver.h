#pragma once

#include <Arduino.h>
#include <ESPAsyncE131.h>
#include "config.h"

// ============================================================================
// sACN Receiver - Receives E1.31 sACN data over WiFi
// ============================================================================

class SACNReceiver {
public:
    // Initialize sACN listener on configured universe(s)
    void begin();

    // Process incoming sACN packets (call from loop on Core 0)
    void loop();

    // Get the sACN data buffer (512 channels)
    uint8_t* getDataBuffer() { return _dataBuffer; }

    // Get the trigger buffer (macro trigger universe)
    const uint8_t* getTriggerBuffer() const { return _triggerBuffer; }

    // Check if a trigger channel is active (value > threshold)
    bool isTriggerActive(uint16_t channel, uint8_t threshold = 128) const;

    // Configuration
    void setDataUniverse(uint16_t universe) { _dataUniverse = universe; }
    void setTriggerUniverse(uint16_t universe) { _triggerUniverse = universe; }
    uint16_t getDataUniverse() const { return _dataUniverse; }
    uint16_t getTriggerUniverse() const { return _triggerUniverse; }

    // Status
    bool isActive() const { return _active; }
    unsigned long getLastPacketTime() const { return _lastPacketTime; }
    unsigned long getPacketCount() const { return _packetCount; }

private:
    ESPAsyncE131 _e131;

    uint8_t _dataBuffer[DMX_CHANNELS]    = {0};  // sACN data universe
    uint8_t _triggerBuffer[DMX_CHANNELS] = {0};  // sACN trigger universe

    uint16_t _dataUniverse    = SACN_DEFAULT_UNIVERSE;
    uint16_t _triggerUniverse = SACN_DEFAULT_TRIGGER_UNIVERSE;

    volatile bool _active = false;
    volatile unsigned long _lastPacketTime = 0;
    volatile unsigned long _packetCount = 0;
    unsigned long _timeoutMs = SACN_TIMEOUT_MS;
};

extern SACNReceiver sacnReceiver;
