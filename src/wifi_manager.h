#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

// ============================================================================
// WiFi Manager - Handles STA connection with AP fallback
// ============================================================================

class WiFiManager {
public:
    // Initialize WiFi. Tries STA mode first, falls back to AP if no credentials.
    void begin();

    // Call periodically to handle reconnection
    void loop();

    // Set STA credentials (saves to config)
    void setCredentials(const String& ssid, const String& password);

    // Get current state
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    bool isAPMode() const { return _apMode; }
    String getIP() const;
    String getSSID() const { return _staSSID; }
    String getMAC() const { return WiFi.macAddress(); }
    int getRSSI() const { return WiFi.RSSI(); }

    // Credentials loaded from config
    String _staSSID;
    String _staPassword;

private:
    void startAP();
    void startSTA();

    bool _apMode = false;
    bool _initialized = false;
    unsigned long _lastReconnectAttempt = 0;
    int _reconnectCount = 0;
};

extern WiFiManager wifiManager;
