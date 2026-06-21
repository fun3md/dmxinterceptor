// ============================================================================
// OSC Receiver - Open Sound Control message handler for DMX Interceptor
// Listens for TouchOSC / OSC client messages and maps them to DMX channels
// ============================================================================
// OSC Address Pattern Reference:
//   /dmxinterceptor/channel/{n}           - Direct DMX channel (1-512), float 0-255
//   /dmxinterceptor/fixture/{id}/dimmer   - Fixture dimmer, float 0-255
//   /dmxinterceptor/fixture/{id}/red      - Fixture red, float 0-255
//   /dmxinterceptor/fixture/{id}/green    - Fixture green, float 0-255
//   /dmxinterceptor/fixture/{id}/blue     - Fixture blue, float 0-255
//   /dmxinterceptor/fixture/{id}/white    - Fixture white, float 0-255
//   /dmxinterceptor/fixture/{id}/warmwhite   - Fixture warm white, float 0-255
//   /dmxinterceptor/fixture/{id}/coolwhite   - Fixture cool white, float 0-255
//   /dmxinterceptor/fixture/{id}/amber   - Fixture amber, float 0-255
//   /dmxinterceptor/fixture/{id}/uv      - Fixture UV, float 0-255
//   /dmxinterceptor/fixture/{id}/strobe  - Fixture strobe, float 0-255
//   /dmxinterceptor/fixture/{id}/pan     - Fixture pan, float 0-255
//   /dmxinterceptor/fixture/{id}/tilt    - Fixture tilt, float 0-255
//   /dmxinterceptor/fixture/{id}/focus   - Fixture focus, float 0-255
//   /dmxinterceptor/fixture/{id}/all     - All fixture channels, float 0-255
//   /dmxinterceptor/group/{id}/dimmer    - Group dimmer, float 0-255
//   /dmxinterceptor/group/{id}/red       - Group red, float 0-255
//   /dmxinterceptor/group/{id}/green     - Group green, float 0-255
//   /dmxinterceptor/group/{id}/blue      - Group blue, float 0-255
//   /dmxinterceptor/group/{id}/white     - Group white, float 0-255
//   /dmxinterceptor/macro/{id}/trigger   - Trigger macro, float > 0
//   /dmxinterceptor/macro/{id}/stop      - Stop macro, float > 0
//   /dmxinterceptor/macro/stopall        - Stop all macros
//   /dmxinterceptor/smoke/trigger        - Trigger smoke, float > 0
//   /dmxinterceptor/smoke/duration       - Smoke duration ms, float
//   /dmxinterceptor/blackout             - Master blackout, float > 0 = on
//   /dmxinterceptor/master               - Master dimmer, float 0-255
// ============================================================================

#include "osc_receiver.h"
#include "fx_engine.h"
#include "config_manager.h"
#include <OSCMessage.h>
#include <OSCBoards.h>

// Global instance
OSCReceiver oscReceiver;

void OSCReceiver::begin() {
    if (!_enabled) {
        Serial.println("[OSC] Disabled, not starting listener");
        return;
    }
    _udp.begin(_port);
    Serial.printf("[OSC] Listening on UDP port %d\n", _port);
    if (_feedbackEnabled) {
        Serial.printf("[OSC] Feedback enabled on port %d\n", _feedbackPort);
    }
}

void OSCReceiver::loop() {
    if (!_enabled) return;

    OSCMessage msg;
    int size = _udp.parsePacket();
    if (size <= 0) return;

    // Read the packet
    while (size--) {
        msg.fill(_udp.read());
    }

    if (msg.hasError()) {
        Serial.println("[OSC] Error parsing message");
        return;
    }

    // Track remote for feedback
    _lastRemoteIP = _udp.remoteIP();
    _lastRemotePort = _udp.remotePort();
    _feedbackIP = _lastRemoteIP;
    _feedbackIPSet = true;
    _packetReceived = true;
    _lastPacketTime = millis();
    _packetCount++;

    // Get the address pattern
    char addr[64];
    msg.getAddress(addr, 0);  // Get full address

    // Route based on address prefix
    if (strncmp(addr, "/dmxinterceptor/channel/", 23) == 0) {
        int32_t ch = atoi(addr + 23);
        handleChannel(msg, ch);
    }
    else if (strncmp(addr, "/dmxinterceptor/fixture/", 24) == 0) {
        // Extract fixture ID: /dmxinterceptor/fixture/{id}/...
        const char* idStart = addr + 24;
        const char* slash = strchr(idStart, '/');
        if (slash) {
            char idStr[8];
            int len = min((int)(slash - idStart), 7);
            strncpy(idStr, idStart, len);
            idStr[len] = '\0';
            int32_t fixtureId = atoi(idStr);
            handleFixture(msg, slash + 1, fixtureId);
        }
    }
    else if (strncmp(addr, "/dmxinterceptor/group/", 22) == 0) {
        const char* idStart = addr + 22;
        const char* slash = strchr(idStart, '/');
        if (slash) {
            char idStr[8];
            int len = min((int)(slash - idStart), 7);
            strncpy(idStr, idStart, len);
            idStr[len] = '\0';
            int32_t groupId = atoi(idStr);
            handleGroup(msg, slash + 1, groupId);
        }
    }
    else if (strncmp(addr, "/dmxinterceptor/macro/", 22) == 0) {
        const char* sub = addr + 22;
        if (strcmp(sub, "stopall") == 0) {
            fxEngine.stopAll();
            Serial.println("[OSC] Stop all macros");
        } else {
            const char* slash = strchr(sub, '/');
            if (slash) {
                char idStr[8];
                int len = min((int)(slash - sub), 7);
                strncpy(idStr, sub, len);
                idStr[len] = '\0';
                int32_t macroId = atoi(idStr);
                handleMacro(msg, slash + 1, macroId);
            }
        }
    }
    else if (strncmp(addr, "/dmxinterceptor/smoke/", 22) == 0) {
        handleSmoke(msg, addr + 22);
    }
    else if (strcmp(addr, "/dmxinterceptor/blackout") == 0) {
        handleBlackout(msg);
    }
    else if (strcmp(addr, "/dmxinterceptor/master") == 0) {
        handleMaster(msg);
    }
    else {
        Serial.printf("[OSC] Unknown address: %s\n", addr);
    }
}

// ============================================================================
// Message Handlers
// ============================================================================

void OSCReceiver::handleChannel(OSCMessage& msg, int32_t channel) {
    if (channel < 1 || channel > DMX_CHANNELS) return;
    uint8_t value = getOSCValue(msg);
    writeOscChannel((uint16_t)(channel - 1), value);
    Serial.printf("[OSC] Channel %d = %d\n", channel, value);
}

void OSCReceiver::handleFixture(OSCMessage& msg, const char* subAddr, int32_t fixtureId) {
    OSCFixtureChannel chType;
    if      (strcmp(subAddr, "dimmer") == 0)    chType = OSC_FCH_DIMMER;
    else if (strcmp(subAddr, "red") == 0)       chType = OSC_FCH_RED;
    else if (strcmp(subAddr, "green") == 0)     chType = OSC_FCH_GREEN;
    else if (strcmp(subAddr, "blue") == 0)      chType = OSC_FCH_BLUE;
    else if (strcmp(subAddr, "white") == 0)     chType = OSC_FCH_WHITE;
    else if (strcmp(subAddr, "warmwhite") == 0) chType = OSC_FCH_WARMWHITE;
    else if (strcmp(subAddr, "coolwhite") == 0) chType = OSC_FCH_COOLWHITE;
    else if (strcmp(subAddr, "amber") == 0)     chType = OSC_FCH_AMBER;
    else if (strcmp(subAddr, "uv") == 0)        chType = OSC_FCH_UV;
    else if (strcmp(subAddr, "strobe") == 0)    chType = OSC_FCH_STROBE;
    else if (strcmp(subAddr, "pan") == 0)       chType = OSC_FCH_PAN;
    else if (strcmp(subAddr, "tilt") == 0)      chType = OSC_FCH_TILT;
    else if (strcmp(subAddr, "focus") == 0)     chType = OSC_FCH_FOCUS;
    else if (strcmp(subAddr, "all") == 0)       chType = OSC_FCH_ALL;
    else {
        Serial.printf("[OSC] Unknown fixture channel: %s\n", subAddr);
        return;
    }

    uint8_t value = getOSCValue(msg);

    if (chType == OSC_FCH_ALL) {
        // Set all channels for this fixture
        const FixtureProfile* f = fixtureManager.getFixture((uint16_t)fixtureId);
        if (!f) return;
        uint16_t base = f->startChannel - 1;
        for (int i = 0; i < f->channelCount && (base + i) < DMX_CHANNELS; i++) {
            writeOscChannel(base + i, value);
        }
        Serial.printf("[OSC] Fixture %d ALL = %d\n", fixtureId, value);
    } else {
        writeFixtureChannel((uint16_t)fixtureId, chType, value);
    }
}

void OSCReceiver::handleGroup(OSCMessage& msg, const char* subAddr, int32_t groupId) {
    OSCFixtureChannel chType;
    if      (strcmp(subAddr, "dimmer") == 0) chType = OSC_FCH_DIMMER;
    else if (strcmp(subAddr, "red") == 0)    chType = OSC_FCH_RED;
    else if (strcmp(subAddr, "green") == 0)  chType = OSC_FCH_GREEN;
    else if (strcmp(subAddr, "blue") == 0)   chType = OSC_FCH_BLUE;
    else if (strcmp(subAddr, "white") == 0)  chType = OSC_FCH_WHITE;
    else {
        Serial.printf("[OSC] Unknown group channel: %s\n", subAddr);
        return;
    }

    uint8_t value = getOSCValue(msg);
    writeGroupChannel((uint8_t)groupId, chType, value);
    Serial.printf("[OSC] Group %d %s = %d\n", groupId, subAddr, value);
}

void OSCReceiver::handleMacro(OSCMessage& msg, const char* subAddr, int32_t macroId) {
    uint8_t value = getOSCValue(msg);
    if (strcmp(subAddr, "trigger") == 0) {
        if (value > 0) {
            fxEngine.triggerMacro((uint16_t)macroId);
            Serial.printf("[OSC] Trigger macro %d\n", macroId);
        }
    } else if (strcmp(subAddr, "stop") == 0) {
        if (value > 0) {
            fxEngine.stopMacro((uint16_t)macroId);
            Serial.printf("[OSC] Stop macro %d\n", macroId);
        }
    }
}

void OSCReceiver::handleSmoke(OSCMessage& msg, const char* subAddr) {
    uint8_t value = getOSCValue(msg);
    if (strcmp(subAddr, "trigger") == 0) {
        if (value > 0) {
            uint16_t ch = configManager.config().smokeDmxChannel - 1;
            if (ch < DMX_CHANNELS) {
                writeOscChannel(ch, 255);
                Serial.printf("[OSC] Smoke trigger (ch %d)\n", ch + 1);
            }
        }
    } else if (strcmp(subAddr, "duration") == 0) {
        // Duration in seconds (0-10), store in config
        uint32_t durationMs = (uint32_t)(msg.getFloat(0) * 1000.0f);
        if (durationMs > SMOKE_MAX_MS) durationMs = SMOKE_MAX_MS;
        configManager.config().smokeBurstMs = durationMs;
        Serial.printf("[OSC] Smoke duration %lu ms\n", durationMs);
    }
}

void OSCReceiver::handleBlackout(OSCMessage& msg) {
    uint8_t value = getOSCValue(msg);
    if (value > 0) {
        // Blackout: set all channels to 0
        memset(_oscBuffer, 0, DMX_CHANNELS);
        memset(_oscMask, 1, DMX_CHANNELS);  // Claim all channels
        Serial.println("[OSC] BLACKOUT ON");
    } else {
        // Release blackout
        memset(_oscMask, 0, DMX_CHANNELS);
        Serial.println("[OSC] BLACKOUT OFF");
    }
}

void OSCReceiver::handleMaster(OSCMessage& msg) {
    uint8_t value = getOSCValue(msg);
    // Master dimmer: set all channels that OSC already controls to this level
    for (int i = 0; i < DMX_CHANNELS; i++) {
        if (_oscMask[i]) {
            _oscBuffer[i] = value;
        }
    }
    Serial.printf("[OSC] Master dimmer = %d\n", value);
}

// ============================================================================
// Helpers
// ============================================================================

uint8_t OSCReceiver::getOSCValue(OSCMessage& msg) {
    float val = 0;
    if (msg.isFloat(0)) val = msg.getFloat(0);
    else if (msg.isInt(0)) val = (float)msg.getInt(0);
    else if (msg.isDouble(0)) val = (float)msg.getDouble(0);
    // Clamp 0-255
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return (uint8_t)val;
}

void OSCReceiver::writeOscChannel(uint16_t dmxCh, uint8_t value) {
    if (dmxCh >= DMX_CHANNELS) return;
    _oscBuffer[dmxCh] = value;
    _oscMask[dmxCh] = 1;
}

void OSCReceiver::writeFixtureChannel(uint16_t fixtureId, OSCFixtureChannel type, uint8_t value) {
    const FixtureProfile* f = fixtureManager.getFixture(fixtureId);
    if (!f || !f->active) return;

    uint8_t offset;
    switch (type) {
        case OSC_FCH_DIMMER:    offset = f->dimmerOffset;    break;
        case OSC_FCH_RED:       offset = f->redOffset;       break;
        case OSC_FCH_GREEN:     offset = f->greenOffset;     break;
        case OSC_FCH_BLUE:      offset = f->blueOffset;      break;
        case OSC_FCH_WHITE:     offset = f->whiteOffset;     break;
        case OSC_FCH_WARMWHITE: offset = f->warmWhiteOffset; break;
        case OSC_FCH_COOLWHITE: offset = f->coolWhiteOffset; break;
        case OSC_FCH_AMBER:     offset = f->amberOffset;     break;
        case OSC_FCH_UV:        offset = f->uvOffset;        break;
        case OSC_FCH_STROBE:    offset = f->strobeOffset;    break;
        case OSC_FCH_PAN:       offset = f->panOffset;       break;
        case OSC_FCH_TILT:      offset = f->tiltOffset;      break;
        case OSC_FCH_FOCUS:     offset = f->focusOffset;     break;
        default: return;
    }

    if (offset == 255) return;  // Channel not available

    uint16_t dmxCh = (f->startChannel - 1) + offset;
    if (dmxCh >= DMX_CHANNELS) return;

    writeOscChannel(dmxCh, value);
    Serial.printf("[OSC] Fixture %d ch[%d] = %d\n", fixtureId, dmxCh + 1, value);
}

void OSCReceiver::writeGroupChannel(uint8_t groupId, OSCFixtureChannel type, uint8_t value) {
    // Find all fixtures in this group and set the channel
    uint16_t ids[MAX_FIXTURES];
    int count = fixtureManager.getFixturesInGroup(groupId, ids, MAX_FIXTURES);
    for (int i = 0; i < count; i++) {
        writeFixtureChannel(ids[i], type, value);
    }
}

// ============================================================================
// Apply OSC buffer to FX overlay
// ============================================================================

void OSCReceiver::applyToFxOverlay(uint8_t* fxOverlay, uint8_t* fxMask) {
    if (!_enabled) return;
    for (int i = 0; i < DMX_CHANNELS; i++) {
        // OSC only writes where FX is NOT already controlling
        // This gives FX macros priority over OSC
        if (_oscMask[i] && !fxMask[i]) {
            fxOverlay[i] = _oscBuffer[i];
            fxMask[i] = 1;
        }
    }
}

// ============================================================================
// Configuration
// ============================================================================

void OSCReceiver::setPort(uint16_t port) {
    _port = port;
    if (_enabled) {
        // Restart listener on new port
        _udp.stop();
        _udp.begin(_port);
        Serial.printf("[OSC] Port changed to %d\n", _port);
    }
}

// ============================================================================
// Feedback (OSC send)
// ============================================================================

void OSCReceiver::sendFeedback(const char* address, float value) {
    if (!_feedbackEnabled || !_feedbackIPSet) return;
    OSCMessage msg(address);
    msg.add(value);
    _udp.beginPacket(_feedbackIP, _feedbackPort);
    msg.send(_udp);
    _udp.endPacket();
    msg.empty();
}

void OSCReceiver::sendFeedback(const char* address, int32_t value) {
    if (!_feedbackEnabled || !_feedbackIPSet) return;
    OSCMessage msg(address);
    msg.add(value);
    _udp.beginPacket(_feedbackIP, _feedbackPort);
    msg.send(_udp);
    _udp.endPacket();
    msg.empty();
}
