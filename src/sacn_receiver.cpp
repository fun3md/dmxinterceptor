#include "sacn_receiver.h"

SACNReceiver sacnReceiver;

void SACNReceiver::begin() {
    Serial.println("[SACN] Initializing sACN/E1.31 receiver...");

    // Listen on both universes via multicast
    // ESPAsyncE131 supports subscribing to a range of universes
    uint16_t startUniverse = min(_dataUniverse, _triggerUniverse);
    uint16_t universeCount = abs((int)_dataUniverse - (int)_triggerUniverse) + 1;

    if (_e131.begin(E131_MULTICAST, startUniverse, universeCount)) {
        Serial.printf("[SACN] Listening on universe %d (data) and %d (triggers)\n",
                      _dataUniverse, _triggerUniverse);
    } else {
        Serial.println("[SACN] ERROR: Failed to start E1.31 listener!");
    }
}

void SACNReceiver::loop() {
    // Process all queued packets
    while (!_e131.isEmpty()) {
        e131_packet_t packet;
        _e131.pull(&packet);

        uint16_t universe = htons(packet.universe);
        uint16_t numChannels = htons(packet.property_value_count) - 1;

        if (numChannels > DMX_CHANNELS) numChannels = DMX_CHANNELS;

        if (universe == _dataUniverse) {
            // Copy sACN data (property_values[0] is the start code, skip it)
            memcpy(_dataBuffer, packet.property_values + 1, numChannels);
            _active = true;
            _lastPacketTime = millis();
            _packetCount++;
        }
        else if (universe == _triggerUniverse) {
            // Copy trigger data
            memcpy(_triggerBuffer, packet.property_values + 1, numChannels);
            _lastPacketTime = millis();
            _packetCount++;
        }
    }

    // Handle timeout - fade sACN data to zero if no packets received
    if (_active && (millis() - _lastPacketTime > _timeoutMs)) {
        Serial.println("[SACN] Timeout - clearing sACN data");
        memset(_dataBuffer, 0, DMX_CHANNELS);
        memset(_triggerBuffer, 0, DMX_CHANNELS);
        _active = false;
    }
}

bool SACNReceiver::isTriggerActive(uint16_t channel, uint8_t threshold) const {
    if (channel < 1 || channel > DMX_CHANNELS) return false;
    return _triggerBuffer[channel - 1] >= threshold;
}
