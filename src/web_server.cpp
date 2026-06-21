#include "web_server.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "dmx_engine.h"
#include "sacn_receiver.h"
#include "fixture_manager.h"
#include "fx_engine.h"
#include "artnet_sender.h"
#include "osc_receiver.h"
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
        doc["artnet"]["enabled"]     = artnetSender.isEnabled();
        doc["artnet"]["packetCount"] = artnetSender.getPacketCount();
        doc["artnet"]["universe"]    = artnetSender.getUniverse();
        doc["artnet"]["source"]      = artnetSender.getSource();
        doc["osc"]["enabled"]        = oscReceiver.isEnabled();
        doc["osc"]["port"]           = oscReceiver.getPort();
        doc["osc"]["feedbackEnabled"] = oscReceiver.isFeedbackEnabled();
        doc["osc"]["feedbackPort"]   = oscReceiver.getFeedbackPort();
        doc["osc"]["packetCount"]    = oscReceiver.getPacketCount();
        doc["osc"]["lastPacketTime"] = oscReceiver.getLastPacketTime();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // --- DMX BUFFERS ---
    // Handlers run on the async-TCP task (Core 0).  DMX buffers are written
    // by the Core 1 DMX tasks, so we use the thread-safe copy helpers to
    // take a consistent snapshot of 513 bytes in one shot.
    _server.on("/api/dmx/input", HTTP_GET, [](AsyncWebServerRequest* req) {
        static uint8_t snapshot[DMX_PACKET_SIZE_FULL];
        dmxEngine.copyInputBuffer(snapshot, DMX_PACKET_SIZE_FULL);
        JsonDocument doc;
        JsonArray ch = doc["channels"].to<JsonArray>();
        for (int i = 1; i <= DMX_CHANNELS; i++) ch.add(snapshot[i]);
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/dmx/output", HTTP_GET, [](AsyncWebServerRequest* req) {
        static uint8_t snapshot[DMX_PACKET_SIZE_FULL];
        dmxEngine.copyOutputBuffer(snapshot, DMX_PACKET_SIZE_FULL);
        JsonDocument doc;
        JsonArray ch = doc["channels"].to<JsonArray>();
        for (int i = 1; i <= DMX_CHANNELS; i++) ch.add(snapshot[i]);
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
            // Apply ArtNet settings
            artnetSender.setEnabled(configManager.config().artnetEnabled);
            artnetSender.setTargetIP(configManager.config().artnetTargetIP);
            artnetSender.setUniverse(configManager.config().artnetUniverse);
            artnetSender.setSource(configManager.config().artnetSource);
            // Apply OSC settings
            oscReceiver.setEnabled(configManager.config().oscEnabled);
            oscReceiver.setPort(configManager.config().oscPort);
            oscReceiver.setFeedbackEnabled(configManager.config().oscFeedbackEnabled);
            oscReceiver.setFeedbackPort(configManager.config().oscFeedbackPort);
            configManager.save();
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
    };
    _server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, configBodyHandler);

    // --- ARTNET ---
    _server.on("/api/artnet", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["enabled"]     = artnetSender.isEnabled();
        doc["targetIP"]    = artnetSender.getTargetIP();
        doc["universe"]    = artnetSender.getUniverse();
        doc["source"]      = artnetSender.getSource();
        doc["packetCount"] = artnetSender.getPacketCount();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/artnet", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return;
            }
            bool    en  = doc["enabled"]  | artnetSender.isEnabled();
            String  ip  = doc["targetIP"] | artnetSender.getTargetIP();
            uint16_t uni = doc["universe"] | artnetSender.getUniverse();
            uint8_t  src = doc["source"]   | artnetSender.getSource();
            artnetSender.setEnabled(en);
            artnetSender.setTargetIP(ip);
            artnetSender.setUniverse(uni);
            artnetSender.setSource(src);
            // Persist
            configManager.config().artnetEnabled  = en;
            configManager.config().artnetTargetIP = ip;
            configManager.config().artnetUniverse = uni;
            configManager.config().artnetSource   = src;
            configManager.save();
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    // --- WIFI CONFIG ---
    _server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["ssid"] = wifiManager.getSSID();
        doc["connected"] = wifiManager.isConnected();
        doc["apMode"] = wifiManager.isAPMode();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return;
            }
            String ssid = doc["ssid"] | "";
            String password = doc["password"] | "";
            bool restart = doc["restart"] | false;

            wifiManager.setCredentials(ssid, password);
            wifiManager.saveCredentials();

            if (restart) {
                req->send(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
                delay(500);
                ESP.restart();
            } else {
                req->send(200, "application/json", "{\"status\":\"ok\"}");
            }
        }
    );

    _server.on("/api/wifi/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
        // Clear stored credentials and reset boot counter
        wifiManager.setCredentials("", "");
        wifiManager.saveCredentials();
        req->send(200, "application/json", "{\"status\":\"cleared\"}");
        delay(500);
        ESP.restart();
    });
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

    // POST /api/fixtures - add or update fixture(s)
    _server.on("/api/fixtures", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            auto processFixture = [](JsonObjectConst obj) -> bool {
                FixtureProfile f;
                strlcpy(f.name, obj["name"] | "Fixture", MAX_FIXTURE_NAME);
                f.startChannel = obj["start"] | 1;
                f.channelCount = obj["count"] | 1;
                f.dimmerOffset = obj["dimmer"] | 255;
                f.redOffset    = obj["red"] | 255;
                f.greenOffset  = obj["green"] | 255;
                f.blueOffset   = obj["blue"] | 255;
                f.whiteOffset  = obj["white"] | 255;
                f.warmWhiteOffset  = obj["warmWhite"] | 255;
                f.coolWhiteOffset  = obj["coolWhite"] | 255;
                f.amberOffset  = obj["amber"] | 255;
                f.uvOffset     = obj["uv"] | 255;
                f.strobeOffset = obj["strobe"] | 255;
                f.panOffset    = obj["pan"] | 255;
                f.tiltOffset   = obj["tilt"] | 255;
                f.focusOffset  = obj["focus"] | 255;
                f.prismOffset  = obj["prism"] | 255;
                f.effectOffset = obj["effect"] | 255;
                f.goboOffset   = obj["gobo"] | 255;
                f.speedOffset  = obj["speed"] | 255;
                f.smokeOffset  = obj["smoke"] | 255;
                f.fanOffset    = obj["fan"] | 255;
                f.groupId      = obj["group"] | 0;

                uint16_t updateId = obj["id"] | 0;
                if (updateId > 0) {
                    return fixtureManager.updateFixture(updateId, f);
                } else {
                    return fixtureManager.addFixture(f);
                }
            };

            bool ok = true;
            if (doc.is<JsonArray>()) {
                for (JsonObjectConst obj : doc.as<JsonArray>()) {
                    if (!processFixture(obj)) { ok = false; }
                }
            } else {
                ok = processFixture(doc.as<JsonObjectConst>());
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

        // --- COLOR PALETTE ---
    _server.on("/api/palette", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray arr = doc["palette"].to<JsonArray>();
        // Palette is defined in fx_engine.h as COLOR_PALETTE
        // We'll hardcode the same values here for the API
        struct { uint8_t r, g, b; const char* name; } colors[] = {
            { 255,   0,   0, "Red" },
            { 255, 128,   0, "Orange" },
            { 255, 255,   0, "Yellow" },
            {   0, 255,   0, "Green" },
            {   0, 255, 255, "Cyan" },
            {   0,   0, 255, "Blue" },
            { 128,   0, 255, "Purple" },
            { 255, 255, 255, "White" },
        };
        for (int i = 0; i < 8; i++) {
            JsonObject c = arr.add<JsonObject>();
            c["r"] = colors[i].r;
            c["g"] = colors[i].g;
            c["b"] = colors[i].b;
            c["name"] = colors[i].name;
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
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
            m.fadeMs    = doc["fade"] | 0;
            m.channelMask = doc["chMask"] | CH_ALL;
            m.paletteIdx = doc["palette"] | 0;
            m.r         = doc["r"] | 255;
            m.g         = doc["g"] | 255;
            m.b         = doc["b"] | 255;
            m.blindR    = doc["blindR"] | 255;
            m.blindG    = doc["blindG"] | 255;
            m.blindB    = doc["blindB"] | 255;
            m.blindW    = doc["blindW"] | 0;
            m.blindWW   = doc["blindWW"] | 0;
            m.blindCW   = doc["blindCW"] | 0;
            m.blindAmber = doc["blindAmber"] | 0;
            m.blindUV   = doc["blindUV"] | 0;
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
            lf.dimmerOffset = doc["dimmer"] | 255;
            lf.redOffset    = doc["red"] | 255;
            lf.greenOffset  = doc["green"] | 255;
            lf.blueOffset   = doc["blue"] | 255;
            lf.whiteOffset  = doc["white"] | 255;
            lf.warmWhiteOffset = doc["warmWhite"] | 255;
            lf.coolWhiteOffset = doc["coolWhite"] | 255;
            lf.amberOffset  = doc["amber"] | 255;
            lf.uvOffset     = doc["uv"] | 255;
            lf.strobeOffset = doc["strobe"] | 255;
            lf.panOffset    = doc["pan"] | 255;
            lf.tiltOffset   = doc["tilt"] | 255;
            lf.focusOffset  = doc["focus"] | 255;
            lf.prismOffset  = doc["prism"] | 255;
            lf.effectOffset = doc["effect"] | 255;
            lf.goboOffset   = doc["gobo"] | 255;
            lf.speedOffset  = doc["speed"] | 255;
            lf.smokeOffset  = doc["smoke"] | 255;
            lf.fanOffset    = doc["fan"] | 255;
            lf.groupId      = doc["group"] | 0;

            fixtureManager.addFixture(lf);
            fixtureManager.stopLearn();
            memset(dmxEngine.getFxOverlay(), 0, DMX_CHANNELS);
            memset(dmxEngine.getFxMask(), 0, DMX_CHANNELS);
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    // --- OSC CONFIG ---
    _server.on("/api/osc", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["enabled"]        = oscReceiver.isEnabled();
        doc["port"]           = oscReceiver.getPort();
        doc["feedbackEnabled"] = oscReceiver.isFeedbackEnabled();
        doc["feedbackPort"]   = oscReceiver.getFeedbackPort();
        doc["packetCount"]    = oscReceiver.getPacketCount();
        doc["lastPacketTime"] = oscReceiver.getLastPacketTime();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/osc", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return;
            }
            bool en       = doc["enabled"]        | oscReceiver.isEnabled();
            uint16_t port = doc["port"]           | oscReceiver.getPort();
            bool fbEn     = doc["feedbackEnabled"] | oscReceiver.isFeedbackEnabled();
            uint16_t fbPort = doc["feedbackPort"] | oscReceiver.getFeedbackPort();
            oscReceiver.setEnabled(en);
            oscReceiver.setPort(port);
            oscReceiver.setFeedbackEnabled(fbEn);
            oscReceiver.setFeedbackPort(fbPort);
            // Persist
            configManager.config().oscEnabled        = en;
            configManager.config().oscPort           = port;
            configManager.config().oscFeedbackEnabled = fbEn;
            configManager.config().oscFeedbackPort    = fbPort;
            configManager.save();
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
