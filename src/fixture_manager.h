#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// Fixture Manager - Fixture profiles, groups, and learning system
// ============================================================================

#define MAX_FIXTURES  32
#define MAX_GROUPS    8
#define MAX_GROUP_NAME 16
#define MAX_FIXTURE_NAME 20

struct FixtureProfile {
    bool     active = false;
    uint16_t id = 0;
    char     name[MAX_FIXTURE_NAME] = "";
    uint16_t startChannel = 0;       // 1-based DMX start address
    uint8_t  channelCount = 1;       // Total channels this fixture uses
    uint8_t  dimmerOffset = 0;       // Offset from startChannel (0-based)
    uint8_t  redOffset    = 255;     // 255 = not available
    uint8_t  greenOffset  = 255;
    uint8_t  blueOffset   = 255;
    uint8_t  whiteOffset  = 255;
    uint8_t  strobeOffset = 255;
    uint8_t  groupId      = 0;      // Which group this fixture belongs to
};

struct FixtureGroup {
    bool active = false;
    uint8_t id = 0;
    char name[MAX_GROUP_NAME] = "";
};

// Learn mode states
enum LearnState {
    LEARN_IDLE,
    LEARN_BLACKOUT,        // Blacked out, waiting for user to start scan
    LEARN_SCANNING,        // Ramping channels one by one
    LEARN_FOUND_START,     // User confirmed light detected, probing footprint
    LEARN_PROBING_DIMMER,  // Testing which channel is the dimmer
    LEARN_PROBING_RGB,     // Testing RGB channels
    LEARN_CONFIRM          // User confirms/edits the detected fixture
};

class FixtureManager {
public:
    // Initialize - load from config
    void begin();

    // Fixture CRUD
    bool addFixture(const FixtureProfile& fixture);
    bool updateFixture(uint16_t id, const FixtureProfile& fixture);
    bool removeFixture(uint16_t id);
    const FixtureProfile* getFixture(uint16_t id) const;
    const FixtureProfile* getFixtures() const { return _fixtures; }
    int getFixtureCount() const;

    // Group CRUD
    bool addGroup(const FixtureGroup& group);
    bool removeGroup(uint8_t id);
    const FixtureGroup* getGroups() const { return _groups; }
    int getGroupCount() const;

    // Get all fixture IDs in a group
    int getFixturesInGroup(uint8_t groupId, uint16_t* outIds, int maxCount) const;

    // Get DMX channels used by a fixture
    void getFixtureChannels(uint16_t fixtureId, uint16_t* dimmer, uint16_t* r, uint16_t* g, uint16_t* b) const;

    // --- Learn Mode ---
    LearnState getLearnState() const { return _learnState; }
    void startLearn();
    void stopLearn();
    uint16_t getLearnCurrentChannel() const { return _learnChannel; }

    // Called by learn mode to set output channels (writes to FX overlay)
    void learnScanNext(uint8_t* fxOverlay, uint8_t* fxMask);
    void learnFoundStart();  // User clicked "light detected"
    void learnProbe(uint8_t* fxOverlay, uint8_t* fxMask);  // Probe footprint

    // Current fixture being learned
    FixtureProfile& getLearnFixture() { return _learnFixture; }

    // Serialization
    void toJson(JsonDocument& doc) const;
    void fromJson(const JsonDocument& doc);
    bool saveToFile();
    bool loadFromFile();

private:
    FixtureProfile _fixtures[MAX_FIXTURES];
    FixtureGroup   _groups[MAX_GROUPS];
    uint16_t _nextFixtureId = 1;

    // Learn mode state
    LearnState _learnState = LEARN_IDLE;
    uint16_t _learnChannel = 1;
    FixtureProfile _learnFixture;
    uint8_t _probeStep = 0;
};

extern FixtureManager fixtureManager;
