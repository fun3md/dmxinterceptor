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
