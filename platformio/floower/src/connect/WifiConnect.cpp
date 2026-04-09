#include "WifiConnect.h"
#include <esp_wifi.h>

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define LOG_TAG ""
#else
#include "esp_log.h"
static const char* LOG_TAG = "WifiConnect";
#endif

#define RECONNECT_INTERVAL_MS 5000
#define CONNECT_RETRY_INTERVAL_MS 30000

// Sanitise a device name into a valid mDNS hostname:
// lowercase, spaces replaced with hyphens, max 63 chars.
static String sanitiseHostname(const String &name) {
    String h = name;
    h.toLowerCase();
    for (size_t i = 0; i < h.length(); i++) {
        char c = h[i];
        if (!isalnum(c) && c != '-') {
            h[i] = '-';
        }
    }
    if (h.length() > 63) {
        h = h.substring(0, 63);
    }
    if (h.isEmpty()) {
        h = "floower";
    }
    return h;
}

WifiConnect::WifiConnect(Config *config, Floower *floower)
        : config(config), floower(floower) {
}

void WifiConnect::setup() {
}

void WifiConnect::enable() {
    if (!config->wifiSsid.isEmpty()) {
        WiFi.mode(WIFI_STA);

        wifiConnectedEventId = WiFi.onEvent(
            [=](WiFiEvent_t event, WiFiEventInfo_t info){ onWifiConnected(event, info); },
            SYSTEM_EVENT_STA_CONNECTED);
        wifiGotIpEventId = WiFi.onEvent(
            [=](WiFiEvent_t event, WiFiEventInfo_t info){ onWifiGotIp(event, info); },
            SYSTEM_EVENT_STA_GOT_IP);
        wifiDisconnectedEventId = WiFi.onEvent(
            [=](WiFiEvent_t event, WiFiEventInfo_t info){ onWifiDisconnected(event, info); },
            SYSTEM_EVENT_STA_DISCONNECTED);

        WiFi.begin(config->wifiSsid.c_str(), config->wifiPassword.c_str());
        ESP_LOGI(LOG_TAG, "WiFi on: %s", config->wifiSsid.c_str());
        wifiOn = true;
        wifiFailed = false;
    }
    enabled = true;
}

void WifiConnect::disable() {
    if (!enabled) {
        return;
    }

    if (server != nullptr) {
        server->end();
        delete server;
        server = nullptr;
        serverRunning = false;
    }
    MDNS.end();

    WiFi.disconnect(true);
    WiFi.removeEvent(wifiConnectedEventId);
    WiFi.removeEvent(wifiGotIpEventId);
    WiFi.removeEvent(wifiDisconnectedEventId);
    esp_wifi_stop();

    ESP_LOGI(LOG_TAG, "WiFi off");
    wifiOn = false;
    wifiConnected = false;
    enabled = false;
}

bool WifiConnect::isEnabled() {
    return enabled;
}

bool WifiConnect::isConnected() {
    return wifiConnected;
}

void WifiConnect::reconnect() {
    if (enabled) {
        if (wifiOn) {
            if (!config->wifiSsid.isEmpty()) {
                wifiFailed = false;
                WiFi.begin(config->wifiSsid.c_str(), config->wifiPassword.c_str());
                ESP_LOGI(LOG_TAG, "WiFi reconnecting: %s", config->wifiSsid.c_str());
            }
            else {
                disable();
            }
        }
        else {
            enable();
        }
    }
}

void WifiConnect::updateFloowerState(int8_t petalsOpenLevel, HsbColor hsbColor) {
    // State is served on demand via GET /api/state; nothing to push.
}

void WifiConnect::updateStatusData(uint8_t batteryLevel, bool batteryCharging) {
    // No remote endpoint to push status to in local mode.
}

uint8_t WifiConnect::getStatus() {
    if (config->wifiSsid.isEmpty()) {
        return WIFI_STATUS_NOT_CONFIGURED;
    }
    if (!enabled) {
        return WIFI_STATUS_DISABLED;
    }
    if (wifiConnected) {
        return WIFI_STATUS_CONNECTED;
    }
    if (wifiFailed) {
        return WIFI_STATUS_FAILED;
    }
    return WIFI_STATUS_CONNECTING;
}

void WifiConnect::onControlCommand(WifiControlCommandCallback callback) {
    controlCommandCallback = callback;
}

bool WifiConnect::isOTAUpdateRunning() {
    return false;
}

void WifiConnect::startOTAUpdate(String firmwareUrl) {
    ESP_LOGW(LOG_TAG, "OTA update via cloud not supported in local mode: %s", firmwareUrl.c_str());
}

void WifiConnect::loop() {
    if (!enabled) {
        return;
    }
    if (reconnectTime > 0 && reconnectTime <= millis()) {
        reconnectTime = 0;
        reconnect();
    }
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------

void WifiConnect::setupRoutes() {
    // GET /api/state
    server->on("/api/state", HTTP_GET,
        [=](AsyncWebServerRequest *request) { handleGetState(request); });

    // POST /api/state  – body arrives in the body callback
    server->on("/api/state", HTTP_POST,
        [](AsyncWebServerRequest *request) {},   // request handler (called after body)
        nullptr,                                  // file upload handler
        [=](AsyncWebServerRequest *request, uint8_t *data, size_t len,
            size_t index, size_t total) {
            handlePostState(request, data, len, index, total);
        });

    // GET /api/info
    server->on("/api/info", HTTP_GET,
        [=](AsyncWebServerRequest *request) { handleGetInfo(request); });

    // POST /api/wifi
    server->on("/api/wifi", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [=](AsyncWebServerRequest *request, uint8_t *data, size_t len,
            size_t index, size_t total) {
            handlePostWifi(request, data, len, index, total);
        });

    // CORS pre-flight
    server->on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(204);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
        request->send(response);
    });

    server->onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });
}

void WifiConnect::startServer() {
    String hostname = sanitiseHostname(config->name);

    // (Re)start mDNS so the hostname stays current after reconnects.
    MDNS.end();
    if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        ESP_LOGI(LOG_TAG, "mDNS started: http://%s.local", hostname.c_str());
    }
    else {
        ESP_LOGW(LOG_TAG, "mDNS failed to start");
    }

    if (!serverRunning) {
        server = new AsyncWebServer(80);
        setupRoutes();
        server->begin();
        serverRunning = true;
        ESP_LOGI(LOG_TAG, "HTTP REST server started (IP: %s)", WiFi.localIP().toString().c_str());
    }
}

// ---------------------------------------------------------------------------
// REST handlers
// ---------------------------------------------------------------------------

void WifiConnect::handleGetState(AsyncWebServerRequest *request) {
    StaticJsonDocument<128> doc;
    RgbColor color = RgbColor(floower->getColor());
    doc["petals"] = floower->getPetalsOpenLevel();
    doc["r"] = color.R;
    doc["g"] = color.G;
    doc["b"] = color.B;

    String body;
    serializeJson(doc, body);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", body);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void WifiConnect::handlePostState(AsyncWebServerRequest *request, uint8_t *data,
                                   size_t len, size_t index, size_t total) {
    // Only process the body when it arrives in a single chunk (expected for small JSON payloads).
    if (index + len < total) {
        request->send(400, "application/json", "{\"error\":\"Request body too large\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    uint16_t duration = config->speedMillis;
    if (doc.containsKey("duration")) {
        duration = doc["duration"].as<uint16_t>();
    }

    bool changed = false;
    if (doc.containsKey("petals")) {
        uint8_t level = doc["petals"].as<uint8_t>();
        if (level <= 100) {
            floower->setPetalsOpenLevel(level, duration);
            changed = true;
        }
    }
    if (doc.containsKey("r") || doc.containsKey("g") || doc.containsKey("b")) {
        uint8_t r = doc.containsKey("r") ? doc["r"].as<uint8_t>() : 0;
        uint8_t g = doc.containsKey("g") ? doc["g"].as<uint8_t>() : 0;
        uint8_t b = doc.containsKey("b") ? doc["b"].as<uint8_t>() : 0;
        HsbColor color = HsbColor(RgbColor(r, g, b));
        floower->transitionColor(color.H, color.S, color.B, duration);
        changed = true;
    }

    if (changed && controlCommandCallback != nullptr) {
        controlCommandCallback();
    }

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void WifiConnect::handleGetInfo(AsyncWebServerRequest *request) {
    StaticJsonDocument<256> doc;
    doc["name"]     = config->name;
    doc["model"]    = config->modelName;
    doc["firmware"] = config->firmwareVersion;
    doc["hardware"] = config->hardwareRevision;
    doc["serial"]   = config->serialNumber;

    String body;
    serializeJson(doc, body);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", body);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void WifiConnect::handlePostWifi(AsyncWebServerRequest *request, uint8_t *data,
                                  size_t len, size_t index, size_t total) {
    // Only process the body when it arrives in a single chunk.
    if (index + len < total) {
        request->send(400, "application/json", "{\"error\":\"Request body too large\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("ssid")) {
        request->send(400, "application/json", "{\"error\":\"Missing ssid\"}");
        return;
    }

    String ssid     = doc["ssid"].as<String>();
    String password = doc.containsKey("password") ? doc["password"].as<String>() : String("");
    config->setWifi(ssid, password);
    config->commit(); // triggers onConfigChanged -> wifiConnect.reconnect()

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// ---------------------------------------------------------------------------
// WiFi event handlers
// ---------------------------------------------------------------------------

void WifiConnect::onWifiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGI(LOG_TAG, "WiFi connected");
}

void WifiConnect::onWifiGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGI(LOG_TAG, "WiFi got IP: %s", WiFi.localIP().toString().c_str());
    reconnectTime = 0;
    wifiConnected = true;
    wifiFailed = false;
    startServer();
}

void WifiConnect::onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (!wifiConnected) {
        ESP_LOGI(LOG_TAG, "WiFi connection failed");
        reconnectTime = millis() + CONNECT_RETRY_INTERVAL_MS;
        wifiFailed = true;
        WiFi.disconnect(true);
    }
    else {
        ESP_LOGI(LOG_TAG, "WiFi disconnected: %d", info.disconnected.reason);
        reconnectTime = millis() + RECONNECT_INTERVAL_MS;
        wifiConnected = false;
        WiFi.disconnect(true);
    }
}

