#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include "config.h"
#include "fixture_manager.h"

// ============================================================================
// OSC Receiver - Listens for Open Sound Control messages over WiFi UDP
// Supports TouchOSC and any OSC client
// ============================================================================

// Fixture channel types for OSC dispatch
enum OSCFixtureChannel : uint8_t {
    OSC_FCH_DIMMER    = 0,
    OSC_FCH_RED       = 1,
    OSC_FCH_GREEN     = 2,
    OSC_FCH_BLUE      = 3,
    OSC_FCH_WHITE     = 4,
    OSC_FCH_WARMWHITE = 5,
    OSC_FCH_COOLWHITE = 6,
    OSC_FCH_AMBER     = 7,
    OSC_FCH_UV        = 8,
    OSC_FCH_STROBE    = 9,
    OSC_FCH_PAN       = 10,
    OSC_FCH_TILT      = 11,
    OSC_FCH_FOCUS     = 12,
    OSC_FCH_PRISM     = 13,
    OSC_FCH_EFFECT    = 14,
    OSC_FCH_GOBO      = 15,
    OSC_FCH_SPEED     = 16,
    OSC_FCH_SMOKE     = 17,
    OSC_FCH_FAN       = 18,
    OSC_FCH_ALL       = 19,
};

class OSCReceiver {
public:
    // Initialize OSC listener on configured port
    void begin();

    // Process incoming OSC packets (call every loop iteration)
    void loop();

    // Apply OSC buffer values into the FX overlay (called from main loop)
    // Only writes to channels where fxMask is NOT already set (FX macros take priority)
    void applyToFxOverlay(uint8_t* fxOverlay, uint8_t* fxMask);

    // Configuration
    void setEnabled(bool en) { _enabled = en; }
    void setPort(uint16_t port);
    void setFeedbackEnabled(bool en) { _feedbackEnabled = en; }
    void setFeedbackPort(uint16_t port) { _feedbackPort = port; }

    // Status
    bool isEnabled() const { return _enabled; }
    uint16_t getPort() const { return _port; }
    bool isFeedbackEnabled() const { return _feedbackEnabled; }
    uint16_t getFeedbackPort() const { return _feedbackPort; }
    bool isPacketReceived() const { return _packetReceived; }
    unsigned long getLastPacketTime() const { return _lastPacketTime; }
    unsigned long getPacketCount() const { return _packetCount; }
    IPAddress getLastRemoteIP() const { return _lastRemoteIP; }
    uint16_t getLastRemotePort() const { return _lastRemotePort; }

    // Send OSC feedback message (e.g. to update TouchOSC fader positions)
    void sendFeedback(const char* address, float value);
    void sendFeedback(const char* address, int32_t value);

private:
    // OSC message handlers
    void handleChannel(OSCMessage& msg, int32_t channel);
    void handleFixture(OSCMessage& msg, const char* subAddr, int32_t fixtureId);
    void handleGroup(OSCMessage& msg, const char* subAddr, int32_t groupId);
    void handleMacro(OSCMessage& msg, const char* subAddr, int32_t macroId);
    void handleSmoke(OSCMessage& msg, const char* subAddr);
    void handleBlackout(OSCMessage& msg);
    void handleMaster(OSCMessage& msg);

    // Helper: get float from OSC message (index 0), clamped 0-255
    uint8_t getOSCValue(OSCMessage& msg);

    // Helper: write a value to the OSC buffer for a specific DMX channel
    void writeOscChannel(uint16_t dmxCh, uint8_t value);

    // Helper: write a fixture channel value using profile offsets
    void writeFixtureChannel(uint16_t fixtureId, OSCFixtureChannel type, uint8_t value);

    // Helper: write a group channel value for all fixtures in the group
    void writeGroupChannel(uint8_t groupId, OSCFixtureChannel type, uint8_t value);

    WiFiUDP _udp;
    bool _enabled = OSC_DEFAULT_ENABLED;
    uint16_t _port = OSC_DEFAULT_PORT;

    // Feedback (OSC send)
    bool _feedbackEnabled = OSC_DEFAULT_FEEDBACK_ENABLED;
    uint16_t _feedbackPort = OSC_DEFAULT_FEEDBACK_PORT;

    // OSC buffer - values written here are applied to fxOverlay each frame
    uint8_t _oscBuffer[DMX_CHANNELS] = {0};
    uint8_t _oscMask[DMX_CHANNELS]   = {0};  // 1 = OSC controls this channel

    // Status
    volatile bool _packetReceived = false;
    volatile unsigned long _lastPacketTime = 0;
    volatile unsigned long _packetCount = 0;
    IPAddress _lastRemoteIP;
    uint16_t _lastRemotePort = 0;

    // Feedback target (last TouchOSC client that sent us a message)
    IPAddress _feedbackIP;
    bool _feedbackIPSet = false;
};

extern OSCReceiver oscReceiver;
