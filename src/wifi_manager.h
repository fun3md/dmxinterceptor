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

    // Save credentials to persistent storage
    void saveCredentials();

    // Check if we have stored credentials
    bool hasCredentials() const { return _staSSID.length() > 0; }

private:
    void startAP();
    void startSTA();

    // Boot-loop detection: if we reboot multiple times without WiFi, fall back to AP
    void resetBootCounter();

    bool _apMode = false;
    bool _initialized = false;
    unsigned long _lastReconnectAttempt = 0;
    int _reconnectCount = 0;

    String _staSSID;
    String _staPassword;
};

extern WiFiManager wifiManager;
