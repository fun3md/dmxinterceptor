#include "wifi_manager.h"
#include <esp_wifi.h>

WiFiManager wifiManager;

void WiFiManager::begin() {
    Serial.println("[WIFI] Initializing WiFi...");

    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.mode(WIFI_STA);

    if (_staSSID.length() > 0) {
        startSTA();
    } else {
        Serial.println("[WIFI] No STA credentials configured, starting AP mode");
        startAP();
    }

    _initialized = true;
}

void WiFiManager::loop() {
    if (!_initialized) return;

    if (_apMode) return;  // In AP mode, nothing to reconnect

    // Check if STA disconnected and try to reconnect
    if (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 10000) {  // Every 10 seconds
            _lastReconnectAttempt = now;
            _reconnectCount++;
            Serial.printf("[WIFI] Reconnecting to %s (attempt %d)...\n",
                          _staSSID.c_str(), _reconnectCount);
            WiFi.disconnect();
            WiFi.begin(_staSSID.c_str(), _staPassword.c_str());

            // Fall back to AP after too many retries
            if (_reconnectCount >= WIFI_MAX_STA_RETRY) {
                Serial.println("[WIFI] Too many failed attempts, falling back to AP mode");
                startAP();
            }
        }
    } else if (_reconnectCount > 0) {
        // Just reconnected
        Serial.printf("[WIFI] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
        _reconnectCount = 0;
    }
}

void WiFiManager::startSTA() {
    _apMode = false;
    _reconnectCount = 0;

    Serial.printf("[WIFI] Connecting to '%s'...\n", _staSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(_staSSID.c_str(), _staPassword.c_str());

    // Wait up to 10 seconds for connection
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI] Hostname: %s\n", WIFI_HOSTNAME);
    } else {
        Serial.println("[WIFI] STA connection failed, starting AP mode");
        startAP();
    }
}

void WiFiManager::startAP() {
    _apMode = true;

    Serial.printf("[WIFI] Starting Access Point: %s\n", WIFI_AP_SSID);
    WiFi.mode(WIFI_AP_STA);  // AP+STA so we can scan for networks
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);

    Serial.printf("[WIFI] AP started! IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WIFI] Connect to WiFi '%s' with password '%s'\n",
                  WIFI_AP_SSID, WIFI_AP_PASSWORD);
}

void WiFiManager::setCredentials(const String& ssid, const String& password) {
    _staSSID = ssid;
    _staPassword = password;
    Serial.printf("[WIFI] Credentials updated: SSID='%s'\n", ssid.c_str());
}

String WiFiManager::getIP() const {
    if (_apMode) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}
