#include "config_manager.h"

ConfigManager configManager;

bool ConfigManager::begin() {
    Serial.println("[CFG] Initializing LittleFS...");

    if (!LittleFS.begin(true)) {  // true = format on first use
        Serial.println("[CFG] ERROR: LittleFS mount failed!");
        return false;
    }

    Serial.printf("[CFG] LittleFS: %d bytes used / %d bytes total\n",
                  LittleFS.usedBytes(), LittleFS.totalBytes());

    return load();
}

bool ConfigManager::load() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("[CFG] No config file found, using defaults");
        resetDefaults();
        return save();  // Create the file with defaults
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("[CFG] ERROR: Could not open config file");
        return false;
    }

    String json = file.readString();
    file.close();

    if (!fromJson(json)) {
        Serial.println("[CFG] ERROR: Failed to parse config, resetting to defaults");
        resetDefaults();
        return save();
    }

    Serial.println("[CFG] Configuration loaded successfully");
    return true;
}

bool ConfigManager::save() {
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[CFG] ERROR: Could not create config file");
        return false;
    }

    String json = toJson();
    file.print(json);
    file.close();

    Serial.printf("[CFG] Configuration saved (%d bytes)\n", json.length());
    return true;
}

void ConfigManager::resetDefaults() {
    _config = AppConfig();  // Reset to struct defaults
    Serial.println("[CFG] Configuration reset to defaults");
}

String ConfigManager::toJson() const {
    JsonDocument doc;

    // WiFi
    doc["wifi"]["ssid"]     = _config.wifiSSID;
    doc["wifi"]["password"] = _config.wifiPassword;
    doc["wifi"]["bootCounter"] = _config.wifiBootCounter;

    // sACN
    doc["sacn"]["dataUniverse"]    = _config.sacnDataUniverse;
    doc["sacn"]["triggerUniverse"] = _config.sacnTriggerUniverse;
    doc["sacn"]["timeoutMs"]       = _config.sacnTimeoutMs;

    // Merge
    doc["merge"]["mode"] = _config.mergeMode;

    // Smoke
    doc["smoke"]["dmxChannel"]  = _config.smokeDmxChannel;
    doc["smoke"]["burstMs"]     = _config.smokeBurstMs;
    doc["smoke"]["cooldownMs"]  = _config.smokeCooldownMs;
    doc["smoke"]["maxMs"]       = _config.smokeMaxMs;

    // ArtNet
    doc["artnet"]["enabled"]  = _config.artnetEnabled;
    doc["artnet"]["targetIP"] = _config.artnetTargetIP;
    doc["artnet"]["universe"] = _config.artnetUniverse;
    doc["artnet"]["source"]   = _config.artnetSource;

    // OSC
    doc["osc"]["enabled"]        = _config.oscEnabled;
    doc["osc"]["port"]           = _config.oscPort;
    doc["osc"]["feedbackEnabled"] = _config.oscFeedbackEnabled;
    doc["osc"]["feedbackPort"]   = _config.oscFeedbackPort;

    String output;
    serializeJsonPretty(doc, output);
    return output;
}

bool ConfigManager::fromJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[CFG] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // WiFi
    _config.wifiSSID     = doc["wifi"]["ssid"] | String("");
    _config.wifiPassword = doc["wifi"]["password"] | String("");
    _config.wifiBootCounter = doc["wifi"]["bootCounter"] | (uint8_t)0;

    // sACN
    _config.sacnDataUniverse    = doc["sacn"]["dataUniverse"] | SACN_DEFAULT_UNIVERSE;
    _config.sacnTriggerUniverse = doc["sacn"]["triggerUniverse"] | SACN_DEFAULT_TRIGGER_UNIVERSE;
    _config.sacnTimeoutMs       = doc["sacn"]["timeoutMs"] | (uint32_t)SACN_TIMEOUT_MS;

    // Merge
    _config.mergeMode = doc["merge"]["mode"] | (uint8_t)MERGE_HTP;

    // Smoke
    _config.smokeDmxChannel  = doc["smoke"]["dmxChannel"] | (uint16_t)SMOKE_DMX_CHANNEL_DEFAULT;
    _config.smokeBurstMs     = doc["smoke"]["burstMs"] | (uint32_t)SMOKE_BURST_MS;
    _config.smokeCooldownMs  = doc["smoke"]["cooldownMs"] | (uint32_t)SMOKE_COOLDOWN_MS;
    _config.smokeMaxMs       = doc["smoke"]["maxMs"] | (uint32_t)SMOKE_MAX_MS;

    // ArtNet
    _config.artnetEnabled  = doc["artnet"]["enabled"]  | false;
    _config.artnetTargetIP = doc["artnet"]["targetIP"] | String(ARTNET_DEFAULT_TARGET);
    _config.artnetUniverse = doc["artnet"]["universe"] | (uint16_t)ARTNET_DEFAULT_UNIVERSE;
    _config.artnetSource   = doc["artnet"]["source"]   | (uint8_t)0;

    // OSC
    _config.oscEnabled        = doc["osc"]["enabled"]        | true;
    _config.oscPort           = doc["osc"]["port"]           | (uint16_t)OSC_DEFAULT_PORT;
    _config.oscFeedbackEnabled = doc["osc"]["feedbackEnabled"] | (bool)OSC_DEFAULT_FEEDBACK_ENABLED;
    _config.oscFeedbackPort   = doc["osc"]["feedbackPort"]   | (uint16_t)OSC_DEFAULT_FEEDBACK_PORT;

    return true;
}
