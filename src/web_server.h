#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// ============================================================================
// Web Server - Async HTTP server with REST API and static file serving
// ============================================================================

class WebServer {
public:
    // Initialize routes and start server
    void begin();

private:
    // API route handlers
    void setupAPIRoutes();
    void setupStaticRoutes();

    AsyncWebServer _server{WEB_SERVER_PORT};
};

extern WebServer webServer;
