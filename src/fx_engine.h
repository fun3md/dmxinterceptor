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

// Channel type bitmask — controls which fixture channels an effect writes to
#define CH_DIMMER    (1 << 0)
#define CH_RED       (1 << 1)
#define CH_GREEN     (1 << 2)
#define CH_BLUE      (1 << 3)
#define CH_WHITE     (1 << 4)
#define CH_WARMWHITE (1 << 5)
#define CH_COOLWHITE (1 << 6)
#define CH_AMBER     (1 << 7)
#define CH_UV        (1 << 8)
#define CH_STROBE    (1 << 9)
#define CH_ALL_COLOR (CH_RED | CH_GREEN | CH_BLUE | CH_WHITE | CH_WARMWHITE | CH_COOLWHITE | CH_AMBER | CH_UV)
#define CH_ALL       (CH_DIMMER | CH_ALL_COLOR | CH_STROBE)

enum FXType : uint8_t {
    FX_NONE = 0,
    FX_BLACKOUT,        // All channels to 0
    FX_BLIND,           // Dimmer + color channels to configurable values
    FX_STROBE,          // Periodic on/off with fade
    FX_RANDOM_STROBE,   // Random fixtures flash with fade
    FX_COLOR,           // Force RGB/color from palette or custom
    FX_SMOKE,           // Activate smoke machine channel
    FX_CHASE,           // Sequential fixture activation with fade
    FX_PULSE            // Sinusoidal breathing with fade
};

// Built-in color palette (8 entries)
struct ColorPaletteEntry {
    uint8_t r, g, b;
    const char* name;
};

static const ColorPaletteEntry COLOR_PALETTE[] = {
    { 255,   0,   0 },  // 0: Red
    { 255, 128,   0 },  // 1: Orange
    { 255, 255,   0 },  // 2: Yellow
    {   0, 255,   0 },  // 3: Green
    {   0, 255, 255 },  // 4: Cyan
    {   0,   0, 255 },  // 5: Blue
    { 128,   0, 255 },  // 6: Purple
    { 255, 255, 255 },  // 7: White
};
#define COLOR_PALETTE_SIZE 8

struct MacroConfig {
    bool     active = false;
    uint16_t id = 0;
    char     name[MAX_MACRO_NAME] = "";
    FXType   type = FX_NONE;
    uint8_t  groupId = 0;            // Which fixture group this affects
    uint16_t sacnTriggerChannel = 0; // sACN channel that triggers this (0=manual only)
    uint8_t  sacnThreshold = 128;    // Threshold for sACN trigger

    // FX parameters
    uint8_t  intensity = 255;        // Effect intensity (master dimmer)
    uint16_t speedMs = 100;          // Speed/rate in ms
    uint16_t fadeMs = 0;             // Fade in/out time in ms (0 = snap)
    uint16_t channelMask = CH_ALL;   // Which channel types to affect (bitmask)

    // Color: either palette index or custom RGB
    uint8_t  paletteIdx = 0;         // 0-7 palette entry, or 255 = use custom r/g/b
    uint8_t  r = 255, g = 255, b = 255; // Custom color (used when paletteIdx == 255)

    // Blind: per-channel override values (0 = off, 255 = full)
    uint8_t  blindR = 255, blindG = 255, blindB = 255;
    uint8_t  blindW = 0, blindWW = 0, blindCW = 0;
    uint8_t  blindAmber = 0, blindUV = 0;

    uint16_t durationMs = 3000;      // Duration for timed effects (smoke)

    // Runtime state (not persisted)
    bool     running = false;
    unsigned long startTime = 0;
    unsigned long lastStepTime = 0;
    uint8_t  stepIndex = 0;
    // Fade runtime
    float    _fadeCurrent = 0;       // Current fade level 0.0-1.0
    float    _fadeDirection = 1;     // 1 = fading in, -1 = fading out
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

    // Helper: apply dimmer value to all fixtures in a group (respects channelMask)
    void setGroupDimmer(uint8_t groupId, uint8_t value, uint8_t* fxOverlay, uint8_t* fxMask, uint16_t mask);
    // Helper: apply RGB values to all fixtures in a group (respects channelMask)
    void setGroupRGB(uint8_t groupId, uint8_t r, uint8_t g, uint8_t b,
                     uint8_t* fxOverlay, uint8_t* fxMask, uint16_t mask);
    // Helper: apply full color (all color channels) respecting channelMask
    void setGroupColor(uint8_t groupId,
                       uint8_t r, uint8_t g, uint8_t b,
                       uint8_t w, uint8_t ww, uint8_t cw,
                       uint8_t amber, uint8_t uv,
                       uint8_t* fxOverlay, uint8_t* fxMask, uint16_t mask);

    // Fade helper: updates _fadeCurrent based on fadeMs and direction, returns 0.0-1.0
    float updateFade(MacroConfig& m, bool isOnPhase);

    MacroConfig _macros[MAX_MACROS];
    uint16_t _nextMacroId = 1;

    // Smoke machine state
    unsigned long _lastSmokeTime = 0;
    bool _smokeCoolingDown = false;
};

extern FXEngine fxEngine;
