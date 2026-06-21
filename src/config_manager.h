#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"

// ============================================================================
// Config Manager - Persists settings to LittleFS as JSON
// ============================================================================

#define CONFIG_FILE "/config.json"

struct AppConfig {
    // WiFi
    String wifiSSID;
    String wifiPassword;
    uint8_t wifiBootCounter = 0;

    // sACN
    uint16_t sacnDataUniverse    = SACN_DEFAULT_UNIVERSE;
    uint16_t sacnTriggerUniverse = SACN_DEFAULT_TRIGGER_UNIVERSE;
    uint32_t sacnTimeoutMs       = SACN_TIMEOUT_MS;

    // Merge
    uint8_t mergeMode = MERGE_HTP;

    // Smoke machine
    uint16_t smokeDmxChannel = SMOKE_DMX_CHANNEL_DEFAULT;
    uint32_t smokeBurstMs    = SMOKE_BURST_MS;
    uint32_t smokeCooldownMs = SMOKE_COOLDOWN_MS;
    uint32_t smokeMaxMs      = SMOKE_MAX_MS;

    // ArtNet output
    bool     artnetEnabled  = ARTNET_DEFAULT_ENABLED;
    String   artnetTargetIP = ARTNET_DEFAULT_TARGET;
    uint16_t artnetUniverse = ARTNET_DEFAULT_UNIVERSE;
    uint8_t  artnetSource   = 0;  // 0 = DMX input, 1 = merged output

    // OSC input
    bool     oscEnabled      = OSC_DEFAULT_ENABLED;
    uint16_t oscPort         = OSC_DEFAULT_PORT;
    bool     oscFeedbackEnabled = OSC_DEFAULT_FEEDBACK_ENABLED;
    uint16_t oscFeedbackPort    = OSC_DEFAULT_FEEDBACK_PORT;
};

class ConfigManager {
public:
    // Initialize LittleFS and load config
    bool begin();

    // Load config from flash
    bool load();

    // Save current config to flash
    bool save();

    // Reset to defaults
    void resetDefaults();

    // Serialize config to JSON string (for API responses)
    String toJson() const;

    // Deserialize JSON string into config (from API requests)
    bool fromJson(const String& json);

    // Access the config
    AppConfig& config() { return _config; }
    const AppConfig& config() const { return _config; }

private:
    AppConfig _config;
};

extern ConfigManager configManager;
