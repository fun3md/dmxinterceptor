#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "fixture_manager.h"

// ============================================================================
// FX Engine - Macro effects applied to fixture groups
// ============================================================================

#define MAX_MACROS 16
#define MAX_MACRO_NAME 20

enum FXType : uint8_t {
    FX_NONE = 0,
    FX_BLACKOUT,        // All channels to 0
    FX_BLIND,           // Dimmer to 255, white
    FX_STROBE,          // Periodic on/off
    FX_RANDOM_STROBE,   // Random fixtures flash
    FX_COLOR,           // Force RGB color
    FX_SMOKE,           // Activate smoke machine channel
    FX_CHASE,           // Sequential fixture activation
    FX_PULSE            // Sinusoidal breathing
};

struct MacroConfig {
    bool     active = false;
    uint16_t id = 0;
    char     name[MAX_MACRO_NAME] = "";
    FXType   type = FX_NONE;
    uint8_t  groupId = 0;           // Which fixture group this affects
    uint16_t sacnTriggerChannel = 0; // sACN channel that triggers this (0=manual only)
    uint8_t  sacnThreshold = 128;   // Threshold for sACN trigger

    // FX parameters
    uint8_t  intensity = 255;       // Effect intensity
    uint16_t speedMs = 100;         // Speed/rate in ms
    uint8_t  r = 255, g = 255, b = 255; // Color for FX_COLOR
    uint16_t durationMs = 3000;     // Duration for timed effects (smoke)

    // Runtime state (not persisted)
    bool     running = false;
    unsigned long startTime = 0;
    unsigned long lastStepTime = 0;
    uint8_t  stepIndex = 0;
};

class FXEngine {
public:
    void begin();

    // Process all active macros (call every loop iteration)
    void process(uint8_t* fxOverlay, uint8_t* fxMask, const uint8_t* triggerBuffer);

    // Manual trigger/stop
    bool triggerMacro(uint16_t id);
    bool stopMacro(uint16_t id);
    void stopAll();

    // Macro CRUD
    bool addMacro(const MacroConfig& macro);
    bool updateMacro(uint16_t id, const MacroConfig& macro);
    bool removeMacro(uint16_t id);
    const MacroConfig* getMacro(uint16_t id) const;
    const MacroConfig* getMacros() const { return _macros; }
    int getMacroCount() const;

    // Serialization
    void toJson(JsonDocument& doc) const;
    void fromJson(const JsonDocument& doc);
    bool saveToFile();
    bool loadFromFile();

private:
    // Per-effect processors
    void processBlackout(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processBlind(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processStrobe(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processRandomStrobe(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processColor(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processSmoke(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processChase(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);
    void processPulse(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask);

    // Helper: apply dimmer value to all fixtures in a group
    void setGroupDimmer(uint8_t groupId, uint8_t value, uint8_t* fxOverlay, uint8_t* fxMask);
    void setGroupRGB(uint8_t groupId, uint8_t r, uint8_t g, uint8_t b,
                     uint8_t* fxOverlay, uint8_t* fxMask);

    MacroConfig _macros[MAX_MACROS];
    uint16_t _nextMacroId = 1;

    // Smoke machine state
    unsigned long _lastSmokeTime = 0;
    bool _smokeCoolingDown = false;
};

extern FXEngine fxEngine;
