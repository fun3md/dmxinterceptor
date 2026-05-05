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
                Serial.printf("[FX] sACN triggered macro '%s'\n", m.name);
            } else if (!triggered && m.running && m.type != FX_SMOKE) {
                // Stop when trigger goes low (except smoke which has its own timing)
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
    setGroupDimmer(m.groupId, 0, fxOverlay, fxMask);
    setGroupRGB(m.groupId, 0, 0, 0, fxOverlay, fxMask);
}

void FXEngine::processBlind(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    setGroupDimmer(m.groupId, m.intensity, fxOverlay, fxMask);
    setGroupRGB(m.groupId, 255, 255, 255, fxOverlay, fxMask);
}

void FXEngine::processStrobe(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastStepTime;

    if (elapsed >= m.speedMs) {
        m.stepIndex = !m.stepIndex;  // Toggle on/off
        m.lastStepTime = now;
    }

    uint8_t val = m.stepIndex ? m.intensity : 0;
    setGroupDimmer(m.groupId, val, fxOverlay, fxMask);
}

void FXEngine::processRandomStrobe(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastStepTime;

    if (elapsed >= m.speedMs) {
        m.lastStepTime = now;

        // Get fixtures in group
        uint16_t ids[MAX_FIXTURES];
        int count = fixtureManager.getFixturesInGroup(m.groupId, ids, MAX_FIXTURES);

        // Randomly flash some fixtures
        for (int i = 0; i < count; i++) {
            const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
            if (!f) continue;

            uint16_t base = f->startChannel - 1;
            bool flash = (random(100) < 40);  // 40% chance to flash

            if (f->dimmerOffset < f->channelCount) {
                fxOverlay[base + f->dimmerOffset] = flash ? m.intensity : 0;
                fxMask[base + f->dimmerOffset] = 1;
            }
        }
    }
}

void FXEngine::processColor(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    setGroupDimmer(m.groupId, m.intensity, fxOverlay, fxMask);
    setGroupRGB(m.groupId, m.r, m.g, m.b, fxOverlay, fxMask);
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
        // Burst complete
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
    }

    // All fixtures off, active fixture on
    for (int i = 0; i < count; i++) {
        const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
        if (!f) continue;

        uint16_t base = f->startChannel - 1;
        uint8_t val = (i == m.stepIndex) ? m.intensity : 0;

        if (f->dimmerOffset < f->channelCount) {
            fxOverlay[base + f->dimmerOffset] = val;
            fxMask[base + f->dimmerOffset] = 1;
        }
    }
}

void FXEngine::processPulse(MacroConfig& m, uint8_t* fxOverlay, uint8_t* fxMask) {
    unsigned long now = millis();
    float phase = (float)(now - m.startTime) / (float)m.speedMs;
    float sineVal = (sin(phase * 2.0f * PI) + 1.0f) / 2.0f;  // 0.0 to 1.0
    uint8_t val = (uint8_t)(sineVal * m.intensity);

    setGroupDimmer(m.groupId, val, fxOverlay, fxMask);
}

// ============================================================================
// Helpers
// ============================================================================

void FXEngine::setGroupDimmer(uint8_t groupId, uint8_t value,
                               uint8_t* fxOverlay, uint8_t* fxMask) {
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
                            uint8_t* fxOverlay, uint8_t* fxMask) {
    uint16_t ids[MAX_FIXTURES];
    int count = fixtureManager.getFixturesInGroup(groupId, ids, MAX_FIXTURES);

    for (int i = 0; i < count; i++) {
        const FixtureProfile* f = fixtureManager.getFixture(ids[i]);
        if (!f) continue;

        uint16_t base = f->startChannel - 1;

        if (f->redOffset != 255 && (base + f->redOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->redOffset] = r;
            fxMask[base + f->redOffset] = 1;
        }
        if (f->greenOffset != 255 && (base + f->greenOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->greenOffset] = g;
            fxMask[base + f->greenOffset] = 1;
        }
        if (f->blueOffset != 255 && (base + f->blueOffset) < DMX_CHANNELS) {
            fxOverlay[base + f->blueOffset] = b;
            fxMask[base + f->blueOffset] = 1;
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
        m["r"]         = _macros[i].r;
        m["g"]         = _macros[i].g;
        m["b"]         = _macros[i].b;
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
        _macros[idx].r = m["r"] | 255;
        _macros[idx].g = m["g"] | 255;
        _macros[idx].b = m["b"] | 255;
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
