#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"

// ============================================================================
// ArtNet Sender - Transmits DMX data as Art-Net ArtDmx packets over WiFi UDP
// Implements Art-Net 4 ArtDmx packet (OpCode 0x5000) - no extra library needed
// ============================================================================

class ArtNetSender {
public:
    // Call after WiFi is connected
    void begin();

    // Send 'channels' DMX values as an ArtDmx packet.
    // dmxData must point to 'channels' bytes (NO start code — channel 1 first).
    void send(const uint8_t* dmxData, uint16_t channels = DMX_CHANNELS);

    // --- Runtime configuration ---
    void setEnabled(bool en)           { _enabled = en; }
    void setTargetIP(const String& ip) { _targetIP = ip; }
    void setUniverse(uint16_t uni)     { _universe = uni; }
    void setSource(uint8_t src)        { _source = src; }  // 0=DMX input, 1=merged output

    // --- Status ---
    bool     isEnabled()     const { return _enabled; }
    String   getTargetIP()   const { return _targetIP; }
    uint16_t getUniverse()   const { return _universe; }
    uint8_t  getSource()     const { return _source; }
    unsigned long getPacketCount() const { return _packetCount; }

private:
    WiFiUDP       _udp;
    bool          _enabled    = ARTNET_DEFAULT_ENABLED;
    String        _targetIP   = ARTNET_DEFAULT_TARGET;
    uint16_t      _universe   = ARTNET_DEFAULT_UNIVERSE;
    uint8_t       _source     = 0;
    uint8_t       _sequence   = 0;        // Cycles 1-255 per Art-Net spec
    unsigned long _packetCount = 0;
    bool          _begun      = false;
};

// Global instance
extern ArtNetSender artnetSender;
