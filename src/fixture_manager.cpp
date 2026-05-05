#include "fixture_manager.h"
#include <LittleFS.h>

#define FIXTURES_FILE "/fixtures.json"

FixtureManager fixtureManager;

void FixtureManager::begin() {
    Serial.println("[FIX] Initializing Fixture Manager...");
    loadFromFile();
    Serial.printf("[FIX] Loaded %d fixtures, %d groups\n", getFixtureCount(), getGroupCount());
}

// --- Fixture CRUD ---

bool FixtureManager::addFixture(const FixtureProfile& fixture) {
    for (int i = 0; i < MAX_FIXTURES; i++) {
        if (!_fixtures[i].active) {
            _fixtures[i] = fixture;
            _fixtures[i].active = true;
            _fixtures[i].id = _nextFixtureId++;
            saveToFile();
            Serial.printf("[FIX] Added fixture '%s' (id=%d, ch=%d, cnt=%d)\n",
                          _fixtures[i].name, _fixtures[i].id,
                          _fixtures[i].startChannel, _fixtures[i].channelCount);
            return true;
        }
    }
    Serial.println("[FIX] ERROR: Max fixtures reached");
    return false;
}

bool FixtureManager::updateFixture(uint16_t id, const FixtureProfile& fixture) {
    for (int i = 0; i < MAX_FIXTURES; i++) {
        if (_fixtures[i].active && _fixtures[i].id == id) {
            uint16_t savedId = _fixtures[i].id;
            _fixtures[i] = fixture;
            _fixtures[i].id = savedId;
            _fixtures[i].active = true;
            saveToFile();
            return true;
        }
    }
    return false;
}

bool FixtureManager::removeFixture(uint16_t id) {
    for (int i = 0; i < MAX_FIXTURES; i++) {
        if (_fixtures[i].active && _fixtures[i].id == id) {
            _fixtures[i].active = false;
            saveToFile();
            return true;
        }
    }
    return false;
}

const FixtureProfile* FixtureManager::getFixture(uint16_t id) const {
    for (int i = 0; i < MAX_FIXTURES; i++) {
        if (_fixtures[i].active && _fixtures[i].id == id) return &_fixtures[i];
    }
    return nullptr;
}

int FixtureManager::getFixtureCount() const {
    int count = 0;
    for (int i = 0; i < MAX_FIXTURES; i++) {
        if (_fixtures[i].active) count++;
    }
    return count;
}

// --- Group CRUD ---

bool FixtureManager::addGroup(const FixtureGroup& group) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!_groups[i].active) {
            _groups[i] = group;
            _groups[i].active = true;
            _groups[i].id = i + 1;
            saveToFile();
            return true;
        }
    }
    return false;
}

bool FixtureManager::removeGroup(uint8_t id) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (_groups[i].active && _groups[i].id == id) {
            _groups[i].active = false;
            // Unassign fixtures from this group
            for (int j = 0; j < MAX_FIXTURES; j++) {
                if (_fixtures[j].active && _fixtures[j].groupId == id) {
                    _fixtures[j].groupId = 0;
                }
            }
            saveToFile();
            return true;
        }
    }
    return false;
}

int FixtureManager::getGroupCount() const {
    int count = 0;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (_groups[i].active) count++;
    }
    return count;
}

int FixtureManager::getFixturesInGroup(uint8_t groupId, uint16_t* outIds, int maxCount) const {
    int count = 0;
    for (int i = 0; i < MAX_FIXTURES && count < maxCount; i++) {
        if (_fixtures[i].active && _fixtures[i].groupId == groupId) {
            outIds[count++] = _fixtures[i].id;
        }
    }
    return count;
}

void FixtureManager::getFixtureChannels(uint16_t fixtureId, uint16_t* dimmer,
                                         uint16_t* r, uint16_t* g, uint16_t* b) const {
    const FixtureProfile* f = getFixture(fixtureId);
    if (!f) return;
    uint16_t base = f->startChannel - 1;  // Convert to 0-based
    *dimmer = (f->dimmerOffset < f->channelCount) ? base + f->dimmerOffset : 0xFFFF;
    *r = (f->redOffset != 255) ? base + f->redOffset : 0xFFFF;
    *g = (f->greenOffset != 255) ? base + f->greenOffset : 0xFFFF;
    *b = (f->blueOffset != 255) ? base + f->blueOffset : 0xFFFF;
}

// --- Learn Mode ---

void FixtureManager::startLearn() {
    _learnState = LEARN_BLACKOUT;
    _learnChannel = 1;
    _probeStep = 0;
    memset(&_learnFixture, 0, sizeof(FixtureProfile));
    Serial.println("[FIX] Learn mode started - blackout");
}

void FixtureManager::stopLearn() {
    _learnState = LEARN_IDLE;
    Serial.println("[FIX] Learn mode stopped");
}

void FixtureManager::learnScanNext(uint8_t* fxOverlay, uint8_t* fxMask) {
    if (_learnState != LEARN_SCANNING) {
        _learnState = LEARN_SCANNING;
    }

    // Clear all channels
    memset(fxOverlay, 0, DMX_CHANNELS);
    memset(fxMask, 0, DMX_CHANNELS);

    if (_learnChannel > DMX_CHANNELS) {
        // Scanned all channels
        _learnState = LEARN_IDLE;
        Serial.println("[FIX] Learn scan complete - no more channels");
        return;
    }

    // Set current channel to full and mask it
    uint16_t ch = _learnChannel - 1;  // 0-based index
    fxOverlay[ch] = 255;
    fxMask[ch] = 1;

    // Also mask all other channels to black (override input)
    for (int i = 0; i < DMX_CHANNELS; i++) {
        fxMask[i] = 1;  // Take control of all channels
    }
    fxOverlay[ch] = 255;  // Only this channel is on

    Serial.printf("[FIX] Learn: testing channel %d\n", _learnChannel);
    _learnChannel++;
}

void FixtureManager::learnFoundStart() {
    if (_learnState != LEARN_SCANNING) return;

    // User saw light on the previous channel (learnChannel was already incremented)
    _learnFixture.startChannel = _learnChannel - 1;
    _learnState = LEARN_PROBING_DIMMER;
    _probeStep = 0;

    Serial.printf("[FIX] Learn: fixture start detected at channel %d\n",
                  _learnFixture.startChannel);
}

void FixtureManager::learnProbe(uint8_t* fxOverlay, uint8_t* fxMask) {
    // Clear all
    memset(fxOverlay, 0, DMX_CHANNELS);
    for (int i = 0; i < DMX_CHANNELS; i++) fxMask[i] = 1;

    uint16_t base = _learnFixture.startChannel - 1;  // 0-based

    // Common fixture footprints to test: 1, 3, 4, 5, 6, 7, 8, 12, 16
    static const uint8_t testFootprints[] = {1, 3, 4, 5, 6, 7, 8, 12, 16};

    if (_learnState == LEARN_PROBING_DIMMER) {
        // Test each channel from start to find dimmer (set one at a time to 255)
        if (_probeStep < 16 && (base + _probeStep) < DMX_CHANNELS) {
            fxOverlay[base + _probeStep] = 255;
            Serial.printf("[FIX] Learn: probing dimmer at offset %d (ch %d)\n",
                          _probeStep, base + _probeStep + 1);
            _probeStep++;
        } else {
            // Done probing dimmer
            _learnState = LEARN_PROBING_RGB;
            _probeStep = 0;
        }
    }
    else if (_learnState == LEARN_PROBING_RGB) {
        // Set dimmer to full, then test RGB one channel at a time
        if (_learnFixture.dimmerOffset < 255) {
            fxOverlay[base + _learnFixture.dimmerOffset] = 255;
        }

        if (_probeStep < 16 && (base + _probeStep) < DMX_CHANNELS) {
            fxOverlay[base + _probeStep] = 255;
            Serial.printf("[FIX] Learn: probing RGB at offset %d (ch %d)\n",
                          _probeStep, base + _probeStep + 1);
            _probeStep++;
        } else {
            _learnState = LEARN_CONFIRM;
        }
    }
}

// --- Serialization ---

void FixtureManager::toJson(JsonDocument& doc) const {
    JsonArray fixtures = doc["fixtures"].to<JsonArray>();
    for (int i = 0; i < MAX_FIXTURES; i++) {
        if (!_fixtures[i].active) continue;
        JsonObject f = fixtures.add<JsonObject>();
        f["id"]     = _fixtures[i].id;
        f["name"]   = _fixtures[i].name;
        f["start"]  = _fixtures[i].startChannel;
        f["count"]  = _fixtures[i].channelCount;
        f["dimmer"] = _fixtures[i].dimmerOffset;
        f["red"]    = _fixtures[i].redOffset;
        f["green"]  = _fixtures[i].greenOffset;
        f["blue"]   = _fixtures[i].blueOffset;
        f["white"]  = _fixtures[i].whiteOffset;
        f["strobe"] = _fixtures[i].strobeOffset;
        f["group"]  = _fixtures[i].groupId;
    }

    JsonArray groups = doc["groups"].to<JsonArray>();
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!_groups[i].active) continue;
        JsonObject g = groups.add<JsonObject>();
        g["id"]   = _groups[i].id;
        g["name"] = _groups[i].name;
    }

    doc["nextId"] = _nextFixtureId;
}

void FixtureManager::fromJson(const JsonDocument& doc) {
    // Clear
    memset(_fixtures, 0, sizeof(_fixtures));
    memset(_groups, 0, sizeof(_groups));

    JsonArrayConst fixtures = doc["fixtures"];
    int idx = 0;
    for (JsonObjectConst f : fixtures) {
        if (idx >= MAX_FIXTURES) break;
        _fixtures[idx].active = true;
        _fixtures[idx].id = f["id"] | 0;
        strlcpy(_fixtures[idx].name, f["name"] | "Fixture", MAX_FIXTURE_NAME);
        _fixtures[idx].startChannel = f["start"] | 1;
        _fixtures[idx].channelCount = f["count"] | 1;
        _fixtures[idx].dimmerOffset = f["dimmer"] | 0;
        _fixtures[idx].redOffset    = f["red"] | 255;
        _fixtures[idx].greenOffset  = f["green"] | 255;
        _fixtures[idx].blueOffset   = f["blue"] | 255;
        _fixtures[idx].whiteOffset  = f["white"] | 255;
        _fixtures[idx].strobeOffset = f["strobe"] | 255;
        _fixtures[idx].groupId      = f["group"] | 0;
        idx++;
    }

    JsonArrayConst groups = doc["groups"];
    idx = 0;
    for (JsonObjectConst g : groups) {
        if (idx >= MAX_GROUPS) break;
        _groups[idx].active = true;
        _groups[idx].id = g["id"] | (idx + 1);
        strlcpy(_groups[idx].name, g["name"] | "Group", MAX_GROUP_NAME);
        idx++;
    }

    _nextFixtureId = doc["nextId"] | 1;
}

bool FixtureManager::saveToFile() {
    JsonDocument doc;
    toJson(doc);

    File file = LittleFS.open(FIXTURES_FILE, "w");
    if (!file) {
        Serial.println("[FIX] ERROR: Could not save fixtures");
        return false;
    }
    serializeJson(doc, file);
    file.close();
    Serial.println("[FIX] Fixtures saved");
    return true;
}

bool FixtureManager::loadFromFile() {
    if (!LittleFS.exists(FIXTURES_FILE)) {
        Serial.println("[FIX] No fixtures file, starting empty");
        return true;
    }

    File file = LittleFS.open(FIXTURES_FILE, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[FIX] ERROR parsing fixtures: %s\n", err.c_str());
        return false;
    }

    fromJson(doc);
    return true;
}
