#include "fx_engine.h"
#include "config_manager.h"
#include <LittleFS.h>

#define MACROS_FILE "/macros.json"

FXEngine fxEngine;

void FXEngine::begin() {
    Serial.println("[FX] Initializing FX Engine...");
    loadFromFile();
    Serial.printf("[FX] Loaded %d macros\n", getMacroCount());
}

// ============================================================================
// Fade helper
// ============================================================================
float FXEngine::updateFade(MacroConfig& m, bool isOnPhase) {
    if (m.fadeMs == 0) return 1.0f;  // No fade = snap

    // Calculate fade increment per ms
    float increment = 1.0f / (float)m.fadeMs;

    if (isOnPhase) {
        m._fadeCurrent += increment * 16.0f;  // Approximate per-call (called ~60fps)
        if (m._fadeCurrent > 1.0f) m._fadeCurrent = 1.0f;
    } else {
        m._fadeCurrent -= increment * 16.0f;
        if (m._fadeCurrent < 0.0f) m._fadeCurrent = 0.0f;
    }
    return m._fadeCurrent;
}

// ============================================================================
// Main process loop — called every cycle from main loop
// ============================================================================
void FXEngine::process(uint8_t* fxOverlay, uint8_t* fxMask, const uint8_t* triggerBuffer) {
    for (int i = 0; i < MAX_MACROS; i++) {
        MacroConfig& m = _macros[i];
        if (!m.active) continue;

        // Check sACN trigger
        if (m.sacnTriggerChannel > 0 && m.sacnTriggerChannel <= DMX_CHANNELS) {
            bool triggered = triggerBuffer[m.sacnTriggerChannel - 1] >= m.sacnThreshold;
            if (triggered && !m.running) {
                m.running = true;
                m.startTime = millis();
                m.lastStepTime = millis();
                m.stepIndex = 0;
                m._fadeCurrent = 0;
                m._fadeDirection = 1;
                Serial.printf("[FX] sACN triggered macro '%s'\n", m.name);
            } else if (!triggered && m.running && m.type != FX_SMOKE) {
                m.running = false;
            }
        }

        if (!m.running) continue;

        // Process active effect
        switch (m.type) {
            case FX_BLACKOUT:      processBlackout(m, fxOverlay, fxMask); break;
            case FX_BLIND:         processBlind(m, fxOverlay, fxMask); break;
            case FX_STROBE:        processStrobe(m, fxOverlay, fxMask); break;
            case FX_RANDOM_STROBE: processRandomStrobe(m, fxOverlay, fxMask); break;
            case FX_COLOR:         processColor(m, fxOverlay, fxMask); break;
            case FX_SMOKE:         processSmoke(m, fxOverlay, fxMask); break;
            case FX_CHASE:         processChase(m, fxOverlay, fxMask); break;
            case FX_PULSE:         processPulse(m, fxOverlay, fxMask); break;
            default: break;
        }
    }
}

// ============================================================================
// Effect Processors
// ============================================================================

void FXEngine::processBlackout(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    uint16_t mask = m.channelMask;
    if (mask & CH_DIMMER) setGroupDimmer(m.groupId, 0, fxOverlay, fxMask, mask);
    if (mask & CH_ALL_COLOR) setGroupRGB(m.groupId, 0, 0, 0, fxOverlay, fxMask, mask);
}

void FXEngine::processBlind(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    uint16_t mask = m.channelMask;
    float fade = updateFade(m, true);

    uint8_t dimVal = (uint8_t)(m.intensity * fade);

    // Apply dimmer
    if (mask & CH_DIMMER) {
        setGroupDimmer(m.groupId, dimVal, fxOverlay, fxMask, mask);
    }

    // Apply per-color-channel blind values (each can be independently set)
    // We use setGroupColor which respects the channelMask
    if (mask & CH_ALL_COLOR) {
        setGroupColor(m.groupId,
            (uint8_t)(m.blindR * fade),
            (uint8_t)(m.blindG * fade),
            (uint8_t)(m.blindB * fade),
            (uint8_t)(m.blindW * fade),
            (uint8_t)(m.blindWW * fade),
            (uint8_t)(m.blindCW * fade),
            (uint8_t)(m.blindAmber * fade),
            (uint8_t)(m.blindUV * fade),
            fxOverlay, fxMask, mask);
    }
}

void FXEngine::processStrobe(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastStepTime;

    if (elapsed >= m.speedMs) {
        m.stepIndex = !m.stepIndex;  // Toggle on/off
        m.lastStepTime = now;
        m._fadeCurrent = 0;  // Reset fade on transition
    }

    float fade = updateFade(m, m.stepIndex != 0);
    uint8_t val = (uint8_t)(m.intensity * fade);
    uint16_t mask = m.channelMask;

    if (mask & CH_DIMMER) setGroupDimmer(m.groupId, val, fxOverlay, fxMask, mask);
    if (mask & CH_ALL_COLOR) setGroupRGB(m.groupId, 255, 255, 255, fxOverlay, fxMask, mask);
}

void FXEngine::processRandomStrobe(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastStepTime;

    if (elapsed >= m.speedMs) {
        m.lastStepTime = now;
        m._fadeCurrent = 0;

        uint16_t ids[MAX_FIXTURES];
        int count = fixtureManager.getFixturesInGroup(m.groupId, ids, MAX_FIXTURES);
        uint16_t mask = m.channelMask;

        for (int i = 0; i < count; i++) {
            const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
            if (!f) continue;

            uint16_t base = f->startChannel - 1;
            bool flash = (random(100) < 40);  // 40% chance to flash

            if (flash) {
                if ((mask & CH_DIMMER) && f->dimmerOffset < f->channelCount) {
                    fxOverlay[base + f->dimmerOffset] = m.intensity;
                    fxMask[base + f->dimmerOffset] = 1;
                }
                if (mask & CH_ALL_COLOR) {
                    if ((mask & CH_RED) && f->redOffset != 255 && (base + f->redOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->redOffset] = 255;
                        fxMask[base + f->redOffset] = 1;
                    }
                    if ((mask & CH_GREEN) && f->greenOffset != 255 && (base + f->greenOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->greenOffset] = 255;
                        fxMask[base + f->greenOffset] = 1;
                    }
                    if ((mask & CH_BLUE) && f->blueOffset != 255 && (base + f->blueOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->blueOffset] = 255;
                        fxMask[base + f->blueOffset] = 1;
                    }
                    if ((mask & CH_WHITE) && f->whiteOffset != 255 && (base + f->whiteOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->whiteOffset] = 255;
                        fxMask[base + f->whiteOffset] = 1;
                    }
                    if ((mask & CH_WARMWHITE) && f->warmWhiteOffset != 255 && (base + f->warmWhiteOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->warmWhiteOffset] = 255;
                        fxMask[base + f->warmWhiteOffset] = 1;
                    }
                    if ((mask & CH_COOLWHITE) && f->coolWhiteOffset != 255 && (base + f->coolWhiteOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->coolWhiteOffset] = 255;
                        fxMask[base + f->coolWhiteOffset] = 1;
                    }
                    if ((mask & CH_AMBER) && f->amberOffset != 255 && (base + f->amberOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->amberOffset] = 255;
                        fxMask[base + f->amberOffset] = 1;
                    }
                    if ((mask & CH_UV) && f->uvOffset != 255 && (base + f->uvOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->uvOffset] = 255;
                        fxMask[base + f->uvOffset] = 1;
                    }
                }
            } else {
                // Off: set to 0 for masked channels
                if ((mask & CH_DIMMER) && f->dimmerOffset < f->channelCount) {
                    fxOverlay[base + f->dimmerOffset] = 0;
                    fxMask[base + f->dimmerOffset] = 1;
                }
                if (mask & CH_ALL_COLOR) {
                    if ((mask & CH_RED) && f->redOffset != 255 && (base + f->redOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->redOffset] = 0; fxMask[base + f->redOffset] = 1;
                    }
                    if ((mask & CH_GREEN) && f->greenOffset != 255 && (base + f->greenOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->greenOffset] = 0; fxMask[base + f->greenOffset] = 1;
                    }
                    if ((mask & CH_BLUE) && f->blueOffset != 255 && (base + f->blueOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->blueOffset] = 0; fxMask[base + f->blueOffset] = 1;
                    }
                    if ((mask & CH_WHITE) && f->whiteOffset != 255 && (base + f->whiteOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->whiteOffset] = 0; fxMask[base + f->whiteOffset] = 1;
                    }
                    if ((mask & CH_WARMWHITE) && f->warmWhiteOffset != 255 && (base + f->warmWhiteOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->warmWhiteOffset] = 0; fxMask[base + f->warmWhiteOffset] = 1;
                    }
                    if ((mask & CH_COOLWHITE) && f->coolWhiteOffset != 255 && (base + f->coolWhiteOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->coolWhiteOffset] = 0; fxMask[base + f->coolWhiteOffset] = 1;
                    }
                    if ((mask & CH_AMBER) && f->amberOffset != 255 && (base + f->amberOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->amberOffset] = 0; fxMask[base + f->amberOffset] = 1;
                    }
                    if ((mask & CH_UV) && f->uvOffset != 255 && (base + f->uvOffset) < DMX_CHANNELS) {
                        fxOverlay[base + f->uvOffset] = 0; fxMask[base + f->uvOffset] = 1;
                    }
                }
            }
        }
    }
}

void FXEngine::processColor(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    uint16_t mask = m.channelMask;
    float fade = updateFade(m, true);

    // Resolve color: palette or custom
    uint8_t cr, cg, cb;
    if (m.paletteIdx < COLOR_PALETTE_SIZE) {
        cr = COLOR_PALETTE[m.paletteIdx].r;
        cg = COLOR_PALETTE[m.paletteIdx].g;
        cb = COLOR_PALETTE[m.paletteIdx].b;
    } else {
        cr = m.r; cg = m.g; cb = m.b;
    }

    uint8_t dimVal = (uint8_t)(m.intensity * fade);

    if (mask & CH_DIMMER) setGroupDimmer(m.groupId, dimVal, fxOverlay, fxMask, mask);
    if (mask & CH_ALL_COLOR) {
        setGroupColor(m.groupId,
            (uint8_t)(cr * fade),
            (uint8_t)(cg * fade),
            (uint8_t)(cb * fade),
            0, 0, 0, 0, 0,
            fxOverlay, fxMask, mask);
    }
}

void FXEngine::processSmoke(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    const AppConfig& cfg = configManager.config();
    uint16_t ch = cfg.smokeDmxChannel - 1;  // 0-based

    if (ch >= DMX_CHANNELS) return;

    // Check cooldown
    if (_smokeCoolingDown && (now - _lastSmokeTime < cfg.smokeCooldownMs)) {
        fxOverlay[ch] = 0;
        fxMask[ch] = 1;
        return;
    }
    _smokeCoolingDown = false;

    // Check duration and safety max
    unsigned long runTime = now - m.startTime;
    if (runTime < cfg.smokeBurstMs && runTime < cfg.smokeMaxMs) {
        fxOverlay[ch] = 255;
        fxMask[ch] = 1;
    } else {
        fxOverlay[ch] = 0;
        fxMask[ch] = 1;
        m.running = false;
        _lastSmokeTime = now;
        _smokeCoolingDown = true;
        Serial.printf("[FX] Smoke burst complete (%lu ms), cooldown started\n", runTime);
    }
}

void FXEngine::processChase(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastStepTime;

    uint16_t ids[MAX_FIXTURES];
    int count = fixtureManager.getFixturesInGroup(m.groupId, ids, MAX_FIXTURES);
    if (count == 0) return;

    if (elapsed >= m.speedMs) {
        m.stepIndex = (m.stepIndex + 1) % count;
        m.lastStepTime = now;
        m._fadeCurrent = 0;  // Reset fade on step change
    }

    float fade = updateFade(m, true);
    uint8_t val = (uint8_t)(m.intensity * fade);
    uint16_t mask = m.channelMask;

    // All fixtures off, active fixture on
    for (int i = 0; i < count; i++) {
        const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
        if (!f) continue;

        uint16_t base = f->startChannel - 1;
        uint8_t v = (i == (int)m.stepIndex) ? val : 0;

        if ((mask & CH_DIMMER) && f->dimmerOffset < f->channelCount) {
            fxOverlay[base + f->dimmerOffset] = v;
            fxMask[base + f->dimmerOffset] = 1;
        }
        if (mask & CH_ALL_COLOR) {
            if ((mask & CH_RED) && f->redOffset != 255 && (base + f->redOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->redOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->redOffset] = 1;
            }
            if ((mask & CH_GREEN) && f->greenOffset != 255 && (base + f->greenOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->greenOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->greenOffset] = 1;
            }
            if ((mask & CH_BLUE) && f->blueOffset != 255 && (base + f->blueOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->blueOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->blueOffset] = 1;
            }
            if ((mask & CH_WHITE) && f->whiteOffset != 255 && (base + f->whiteOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->whiteOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->whiteOffset] = 1;
            }
            if ((mask & CH_WARMWHITE) && f->warmWhiteOffset != 255 && (base + f->warmWhiteOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->warmWhiteOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->warmWhiteOffset] = 1;
            }
            if ((mask & CH_COOLWHITE) && f->coolWhiteOffset != 255 && (base + f->coolWhiteOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->coolWhiteOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->coolWhiteOffset] = 1;
            }
            if ((mask & CH_AMBER) && f->amberOffset != 255 && (base + f->amberOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->amberOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->amberOffset] = 1;
            }
            if ((mask & CH_UV) && f->uvOffset != 255 && (base + f->uvOffset) < DMX_CHANNELS) {
                fxOverlay[base + f->uvOffset] = (i == (int)m.stepIndex) ? 255 : 0;
                fxMask[base + f->uvOffset] = 1;
            }
        }
    }
}

void FXEngine::processPulse(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    float phase = (float)(now - m.startTime) / (float)m.speedMs;
    float sineVal = (sin(phase * 2.0f * PI) + 1.0f) / 2.0f;  // 0.0 to 1.0
    uint8_t val = (uint8_t)(sineVal * m.intensity);
    uint16_t mask = m.channelMask;

    if (mask & CH_DIMMER) setGroupDimmer(m.groupId, val, fxOverlay, fxMask, mask);
    if (mask & CH_ALL_COLOR) setGroupRGB(m.groupId, 255, 255, 255, fxOverlay, fxMask, mask);
}

// ============================================================================
// Helpers
// ============================================================================

void FXEngine::setGroupDimmer(uint8_t groupId, uint8_t value,
                               uint8_t* fxOverlay, uint8_t* fxMask, uint16_t mask) {
    if (!(mask & CH_DIMMER)) return;
    uint16_t ids[MAX_FIXTURES];
    int count = fixtureManager.getFixturesInGroup(groupId, ids, MAX_FIXTURES);

    for (int i = 0; i < count; i++) {
        const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
        if (!f || f->dimmerOffset >= f->channelCount) continue;

        uint16_t ch = f->startChannel - 1 + f->dimmerOffset;
        if (ch < DMX_CHANNELS) {
            fxOverlay[ch] = value;
            fxMask[ch] = 1;
        }
    }
}

void FXEngine::setGroupRGB(uint8_t groupId, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t* fxOverlay, uint8_t* fxMask, uint16_t mask) {
    uint16_t ids[MAX_FIXTURES];
    int count = fixtureManager.getFixturesInGroup(groupId, ids, MAX_FIXTURES);

    for (int i = 0; i < count; i++) {
        const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
        if (!f) continue;

        uint16_t base = f->startChannel - 1;

        if ((mask & CH_RED) && f->redOffset != 255 && (base + f->redOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->redOffset] = r;
            fxMask[base + f->redOffset] = 1;
        }
        if ((mask & CH_GREEN) && f->greenOffset != 255 && (base + f->greenOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->greenOffset] = g;
            fxMask[base + f->greenOffset] = 1;
        }
        if ((mask & CH_BLUE) && f->blueOffset != 255 && (base + f->blueOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->blueOffset] = b;
            fxMask[base + f->blueOffset] = 1;
        }
    }
}

void FXEngine::setGroupColor(uint8_t groupId,
                              uint8_t r, uint8_t g, uint8_t b,
                              uint8_t w, uint8_t ww, uint8_t cw,
                              uint8_t amber, uint8_t uv,
                              uint8_t* fxOverlay, uint8_t* fxMask, uint16_t mask) {
    uint16_t ids[MAX_FIXTURES];
    int count = fixtureManager.getFixturesInGroup(groupId, ids, MAX_FIXTURES);

    for (int i = 0; i < count; i++) {
        const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
        if (!f) continue;

        uint16_t base = f->startChannel - 1;

        if ((mask & CH_RED) && f->redOffset != 255 && (base + f->redOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->redOffset] = r;
            fxMask[base + f->redOffset] = 1;
        }
        if ((mask & CH_GREEN) && f->greenOffset != 255 && (base + f->greenOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->greenOffset] = g;
            fxMask[base + f->greenOffset] = 1;
        }
        if ((mask & CH_BLUE) && f->blueOffset != 255 && (base + f->blueOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->blueOffset] = b;
            fxMask[base + f->blueOffset] = 1;
        }
        if ((mask & CH_WHITE) && f->whiteOffset != 255 && (base + f->whiteOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->whiteOffset] = w;
            fxMask[base + f->whiteOffset] = 1;
        }
        if ((mask & CH_WARMWHITE) && f->warmWhiteOffset != 255 && (base + f->warmWhiteOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->warmWhiteOffset] = ww;
            fxMask[base + f->warmWhiteOffset] = 1;
        }
        if ((mask & CH_COOLWHITE) && f->coolWhiteOffset != 255 && (base + f->coolWhiteOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->coolWhiteOffset] = cw;
            fxMask[base + f->coolWhiteOffset] = 1;
        }
        if ((mask & CH_AMBER) && f->amberOffset != 255 && (base + f->amberOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->amberOffset] = amber;
            fxMask[base + f->amberOffset] = 1;
        }
        if ((mask & CH_UV) && f->uvOffset != 255 && (base + f->uvOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->uvOffset] = uv;
            fxMask[base + f->uvOffset] = 1;
        }
    }
}

// ============================================================================
// Macro CRUD
// ============================================================================

bool FXEngine::triggerMacro(uint16_t id) {
    for (int i = 0; i < MAX_MACROS; i++) {
        if (_macros[i].active && _macros[i].id == id) {
            _macros[i].running = true;
            _macros[i].startTime = millis();
            _macros[i].lastStepTime = millis();
            _macros[i].stepIndex = 0;
            _macros[i]._fadeCurrent = 0;
            _macros[i]._fadeDirection = 1;
            Serial.printf("[FX] Triggered macro '%s'\n", _macros[i].name);
            return true;
        }
    }
    return false;
}

bool FXEngine::stopMacro(uint16_t id) {
    for (int i = 0; i < MAX_MACROS; i++) {
        if (_macros[i].active && _macros[i].id == id) {
            _macros[i].running = false;
            Serial.printf("[FX] Stopped macro '%s'\n", _macros[i].name);
            return true;
        }
    }
    return false;
}

void FXEngine::stopAll() {
    for (int i = 0; i < MAX_MACROS; i++) {
        _macros[i].running = false;
    }
    Serial.println("[FX] All macros stopped");
}

bool FXEngine::addMacro(const MacroConfig& macro) {
    for (int i = 0; i < MAX_MACROS; i++) {
        if (!_macros[i].active) {
            _macros[i] = macro;
            _macros[i].active = true;
            _macros[i].id = _nextMacroId++;
            _macros[i].running = false;
            saveToFile();
            return true;
        }
    }
    return false;
}

bool FXEngine::updateMacro(uint16_t id, const MacroConfig& macro) {
    for (int i = 0; i < MAX_MACROS; i++) {
        if (_macros[i].active && _macros[i].id == id) {
            bool wasRunning = _macros[i].running;
            uint16_t savedId = _macros[i].id;
            _macros[i] = macro;
            _macros[i].id = savedId;
            _macros[i].active = true;
            _macros[i].running = wasRunning;
            saveToFile();
            return true;
        }
    }
    return false;
}

bool FXEngine::removeMacro(uint16_t id) {
    for (int i = 0; i < MAX_MACROS; i++) {
        if (_macros[i].active && _macros[i].id == id) {
            _macros[i].active = false;
            _macros[i].running = false;
            saveToFile();
            return true;
        }
    }
    return false;
}

const MacroConfig* FXEngine::getMacro(uint16_t id) const {
    for (int i = 0; i < MAX_MACROS; i++) {
        if (_macros[i].active && _macros[i].id == id) return &_macros[i];
    }
    return nullptr;
}

int FXEngine::getMacroCount() const {
    int count = 0;
    for (int i = 0; i < MAX_MACROS; i++) {
        if (_macros[i].active) count++;
    }
    return count;
}

// ============================================================================
// Serialization
// ============================================================================

void FXEngine::toJson(JsonDocument& doc) const {
    JsonArray macros = doc["macros"].to<JsonArray>();
    for (int i = 0; i < MAX_MACROS; i++) {
        if (!_macros[i].active) continue;
        JsonObject m = macros.add<JsonObject>();
        m["id"]        = _macros[i].id;
        m["name"]      = _macros[i].name;
        m["type"]      = (uint8_t)_macros[i].type;
        m["group"]     = _macros[i].groupId;
        m["triggerCh"] = _macros[i].sacnTriggerChannel;
        m["threshold"] = _macros[i].sacnThreshold;
        m["intensity"] = _macros[i].intensity;
        m["speed"]     = _macros[i].speedMs;
        m["fade"]      = _macros[i].fadeMs;
        m["chMask"]    = _macros[i].channelMask;
        m["palette"]   = _macros[i].paletteIdx;
        m["r"]         = _macros[i].r;
        m["g"]         = _macros[i].g;
        m["b"]         = _macros[i].b;
        m["blindR"]    = _macros[i].blindR;
        m["blindG"]    = _macros[i].blindG;
        m["blindB"]    = _macros[i].blindB;
        m["blindW"]    = _macros[i].blindW;
        m["blindWW"]   = _macros[i].blindWW;
        m["blindCW"]   = _macros[i].blindCW;
        m["blindAmber"]= _macros[i].blindAmber;
        m["blindUV"]   = _macros[i].blindUV;
        m["duration"]  = _macros[i].durationMs;
    }
    doc["nextId"] = _nextMacroId;
}

void FXEngine::fromJson(const JsonDocument& doc) {
    memset(_macros, 0, sizeof(_macros));

    JsonArrayConst macros = doc["macros"];
    int idx = 0;
    for (JsonObjectConst m : macros) {
        if (idx >= MAX_MACROS) break;
        _macros[idx].active = true;
        _macros[idx].id = m["id"] | 0;
        strlcpy(_macros[idx].name, m["name"] | "Macro", MAX_MACRO_NAME);
        _macros[idx].type = (FXType)(m["type"] | 0);
        _macros[idx].groupId = m["group"] | 0;
        _macros[idx].sacnTriggerChannel = m["triggerCh"] | 0;
        _macros[idx].sacnThreshold = m["threshold"] | 128;
        _macros[idx].intensity = m["intensity"] | 255;
        _macros[idx].speedMs = m["speed"] | 100;
        _macros[idx].fadeMs = m["fade"] | 0;
        _macros[idx].channelMask = m["chMask"] | CH_ALL;
        _macros[idx].paletteIdx = m["palette"] | 0;
        _macros[idx].r = m["r"] | 255;
        _macros[idx].g = m["g"] | 255;
        _macros[idx].b = m["b"] | 255;
        _macros[idx].blindR = m["blindR"] | 255;
        _macros[idx].blindG = m["blindG"] | 255;
        _macros[idx].blindB = m["blindB"] | 255;
        _macros[idx].blindW = m["blindW"] | 0;
        _macros[idx].blindWW = m["blindWW"] | 0;
        _macros[idx].blindCW = m["blindCW"] | 0;
        _macros[idx].blindAmber = m["blindAmber"] | 0;
        _macros[idx].blindUV = m["blindUV"] | 0;
        _macros[idx].durationMs = m["duration"] | 3000;
        _macros[idx].running = false;
        idx++;
    }
    _nextMacroId = doc["nextId"] | 1;
}

bool FXEngine::saveToFile() {
    JsonDocument doc;
    toJson(doc);
    File file = LittleFS.open(MACROS_FILE, "w");
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    Serial.println("[FX] Macros saved");
    return true;
}

bool FXEngine::loadFromFile() {
    if (!LittleFS.exists(MACROS_FILE)) return true;
    File file = LittleFS.open(MACROS_FILE, "r");
    if (!file) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;
    fromJson(doc);
    return true;
}
