#include "web_server.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "dmx_engine.h"
#include "sacn_receiver.h"
#include "fixture_manager.h"
#include "fx_engine.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

WebServer webServer;

// Helper to parse JSON body from POST requests
static String _bodyBuffer;

void WebServer::begin() {
    Serial.println("[WEB] Initializing web server...");
    setupAPIRoutes();
    setupStaticRoutes();
    _server.begin();
    Serial.printf("[WEB] Server on port %d\n", WEB_SERVER_PORT);
}

void WebServer::setupAPIRoutes() {
    // Add CORS headers
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    // --- STATUS ---
    _server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["uptime"]    = millis() / 1000;
        doc["freeHeap"]  = ESP.getFreeHeap();
        doc["wifi"]["connected"] = wifiManager.isConnected();
        doc["wifi"]["apMode"]    = wifiManager.isAPMode();
        doc["wifi"]["ip"]        = wifiManager.getIP();
        doc["wifi"]["ssid"]      = wifiManager.getSSID();
        doc["wifi"]["rssi"]      = wifiManager.getRSSI();
        doc["dmx"]["inputActive"]  = dmxEngine.isDmxInputActive();
        doc["dmx"]["rxFps"]        = dmxEngine.getRxFps();
        doc["dmx"]["txFps"]        = dmxEngine.getTxFps();
        doc["dmx"]["rxPackets"]    = dmxEngine.getRxPacketCount();
        doc["dmx"]["mergeMode"]    = dmxEngine.getMergeMode() == MERGE_HTP ? "HTP" : "LTP";
        doc["sacn"]["active"]       = sacnReceiver.isActive();
        doc["sacn"]["packets"]      = sacnReceiver.getPacketCount();
        doc["sacn"]["dataUniverse"] = sacnReceiver.getDataUniverse();
        doc["sacn"]["triggerUniverse"] = sacnReceiver.getTriggerUniverse();
        doc["fixtures"]["count"] = fixtureManager.getFixtureCount();
        doc["fixtures"]["groups"] = fixtureManager.getGroupCount();
        doc["macros"]["count"] = fxEngine.getMacroCount();
        doc["learn"]["state"] = (int)fixtureManager.getLearnState();
        doc["learn"]["channel"] = fixtureManager.getLearnCurrentChannel();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // --- DMX BUFFERS ---
    _server.on("/api/dmx/input", HTTP_GET, [](AsyncWebServerRequest* req) {
        const uint8_t* buf = dmxEngine.getInputBuffer();
        JsonDocument doc;
        JsonArray ch = doc["channels"].to<JsonArray>();
        for (int i = 1; i <= DMX_CHANNELS; i++) ch.add(buf[i]);
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/dmx/output", HTTP_GET, [](AsyncWebServerRequest* req) {
        const uint8_t* buf = dmxEngine.getOutputBuffer();
        JsonDocument doc;
        JsonArray ch = doc["channels"].to<JsonArray>();
        for (int i = 1; i <= DMX_CHANNELS; i++) ch.add(buf[i]);
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // --- CONFIG ---
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", configManager.toJson());
    });

    auto configBodyHandler = [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        String body = String((char*)data, len);
        if (configManager.fromJson(body)) {
            wifiManager.setCredentials(configManager.config().wifiSSID, configManager.config().wifiPassword);
            sacnReceiver.setDataUniverse(configManager.config().sacnDataUniverse);
            sacnReceiver.setTriggerUniverse(configManager.config().sacnTriggerUniverse);
            dmxEngine.setMergeMode(configManager.config().mergeMode);
            configManager.save();
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
    };
    _server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, configBodyHandler);

    // --- WIFI SCAN ---
    _server.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest* req) {
        int n = WiFi.scanNetworks(false, false);
        JsonDocument doc;
        JsonArray nets = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject net = nets.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        WiFi.scanDelete();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // --- FIXTURES ---
    _server.on("/api/fixtures", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        fixtureManager.toJson(doc);
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /api/fixtures - add or update fixture
    _server.on("/api/fixtures", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            FixtureProfile f;
            strlcpy(f.name, doc["name"] | "Fixture", MAX_FIXTURE_NAME);
            f.startChannel = doc["start"] | 1;
            f.channelCount = doc["count"] | 1;
            f.dimmerOffset = doc["dimmer"] | 0;
            f.redOffset    = doc["red"] | 255;
            f.greenOffset  = doc["green"] | 255;
            f.blueOffset   = doc["blue"] | 255;
            f.whiteOffset  = doc["white"] | 255;
            f.strobeOffset = doc["strobe"] | 255;
            f.groupId      = doc["group"] | 0;

            uint16_t updateId = doc["id"] | 0;
            if (updateId > 0) {
                fixtureManager.updateFixture(updateId, f);
            } else {
                fixtureManager.addFixture(f);
            }
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    // DELETE /api/fixtures?id=X
    _server.on("/api/fixtures", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("id")) { req->send(400, "application/json", "{\"error\":\"Missing id\"}"); return; }
        uint16_t id = req->getParam("id")->value().toInt();
        fixtureManager.removeFixture(id);
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // --- GROUPS ---
    _server.on("/api/groups", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return;
            }
            FixtureGroup g;
            strlcpy(g.name, doc["name"] | "Group", MAX_GROUP_NAME);
            fixtureManager.addGroup(g);
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    _server.on("/api/groups", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("id")) { req->send(400, "application/json", "{\"error\":\"Missing id\"}"); return; }
        fixtureManager.removeGroup(req->getParam("id")->value().toInt());
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // --- MACROS ---
    _server.on("/api/macros", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        fxEngine.toJson(doc);
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/macros", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return;
            }
            MacroConfig m;
            strlcpy(m.name, doc["name"] | "Macro", MAX_MACRO_NAME);
            m.type      = (FXType)(doc["type"] | 0);
            m.groupId   = doc["group"] | 0;
            m.sacnTriggerChannel = doc["triggerCh"] | 0;
            m.sacnThreshold = doc["threshold"] | 128;
            m.intensity = doc["intensity"] | 255;
            m.speedMs   = doc["speed"] | 100;
            m.r         = doc["r"] | 255;
            m.g         = doc["g"] | 255;
            m.b         = doc["b"] | 255;
            m.durationMs = doc["duration"] | 3000;

            uint16_t updateId = doc["id"] | 0;
            if (updateId > 0) {
                fxEngine.updateMacro(updateId, m);
            } else {
                fxEngine.addMacro(m);
            }
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    _server.on("/api/macros", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("id")) { req->send(400, "application/json", "{\"error\":\"Missing id\"}"); return; }
        fxEngine.removeMacro(req->getParam("id")->value().toInt());
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // POST /api/macros/trigger?id=X
    _server.on("/api/macros/trigger", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("id")) { req->send(400, "application/json", "{\"error\":\"Missing id\"}"); return; }
        uint16_t id = req->getParam("id")->value().toInt();
        fxEngine.triggerMacro(id);
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    _server.on("/api/macros/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (req->hasParam("id")) {
            fxEngine.stopMacro(req->getParam("id")->value().toInt());
        } else {
            fxEngine.stopAll();
        }
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // --- LEARN MODE ---
    _server.on("/api/learn/start", HTTP_POST, [](AsyncWebServerRequest* req) {
        fixtureManager.startLearn();
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    _server.on("/api/learn/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        fixtureManager.stopLearn();
        // Clear FX overlay
        memset(dmxEngine.getFxOverlay(), 0, DMX_CHANNELS);
        memset(dmxEngine.getFxMask(), 0, DMX_CHANNELS);
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    _server.on("/api/learn/next", HTTP_POST, [](AsyncWebServerRequest* req) {
        // Advance to next channel in scan
        fixtureManager.learnScanNext(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());
        JsonDocument doc;
        doc["channel"] = fixtureManager.getLearnCurrentChannel();
        doc["state"]   = (int)fixtureManager.getLearnState();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/learn/found", HTTP_POST, [](AsyncWebServerRequest* req) {
        fixtureManager.learnFoundStart();
        JsonDocument doc;
        doc["startChannel"] = fixtureManager.getLearnFixture().startChannel;
        doc["state"] = (int)fixtureManager.getLearnState();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/learn/probe", HTTP_POST, [](AsyncWebServerRequest* req) {
        fixtureManager.learnProbe(dmxEngine.getFxOverlay(), dmxEngine.getFxMask());
        JsonDocument doc;
        doc["state"] = (int)fixtureManager.getLearnState();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/learn/confirm", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return;
            }
            // Save the learned fixture with user edits
            FixtureProfile& lf = fixtureManager.getLearnFixture();
            strlcpy(lf.name, doc["name"] | "Learned", MAX_FIXTURE_NAME);
            lf.channelCount = doc["count"] | lf.channelCount;
            lf.dimmerOffset = doc["dimmer"] | lf.dimmerOffset;
            lf.redOffset    = doc["red"] | lf.redOffset;
            lf.greenOffset  = doc["green"] | lf.greenOffset;
            lf.blueOffset   = doc["blue"] | lf.blueOffset;
            lf.groupId      = doc["group"] | 0;

            fixtureManager.addFixture(lf);
            fixtureManager.stopLearn();
            memset(dmxEngine.getFxOverlay(), 0, DMX_CHANNELS);
            memset(dmxEngine.getFxMask(), 0, DMX_CHANNELS);
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    // --- SMOKE TRIGGER ---
    _server.on("/api/smoke/trigger", HTTP_POST, [](AsyncWebServerRequest* req) {
        // Create a temporary smoke macro and trigger it
        MacroConfig smoke;
        smoke.active = true;
        smoke.type = FX_SMOKE;
        strlcpy(smoke.name, "_smoke", MAX_MACRO_NAME);
        // Trigger via the FX engine by directly setting the channel
        uint16_t ch = configManager.config().smokeDmxChannel - 1;
        if (ch < DMX_CHANNELS) {
            dmxEngine.getFxOverlay()[ch] = 255;
            dmxEngine.getFxMask()[ch] = 1;
            // Schedule off after burst duration
        }
        req->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // --- RESTART ---
    _server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(500);
        ESP.restart();
    });

    // --- CORS ---
    _server.on("/api/*", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(204);
        res->addHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        req->send(res);
    });
}

void WebServer::setupStaticRoutes() {
    _server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    _server.onNotFound([](AsyncWebServerRequest* req) {
        if (req->url().startsWith("/api/")) {
            req->send(404, "application/json", "{\"error\":\"Not found\"}");
        } else {
            req->send(LittleFS, "/www/index.html", "text/html");
        }
    });
}
