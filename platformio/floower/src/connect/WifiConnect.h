#pragma once

#include "Arduino.h"
#include "Config.h"
#include "hardware/Floower.h"
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "ArduinoJson.h"
#include <ESPmDNS.h>
#include <functional>

// network status (values kept for BLE backward compatibility)
#define WIFI_STATUS_DISABLED 0
#define WIFI_STATUS_NOT_CONFIGURED 1
#define WIFI_STATUS_FAILED 2
#define WIFI_STATUS_CONNECTING 5
#define WIFI_STATUS_CONNECTED 4

typedef std::function<void()> WifiControlCommandCallback;

class WifiConnect {
    public:
        WifiConnect(Config *config, Floower *floower);
        void setup();
        void loop();
        void enable();
        void disable();
        void reconnect();
        void updateFloowerState(int8_t petalsOpenLevel, HsbColor hsbColor);
        void updateStatusData(uint8_t batteryLevel, bool batteryCharging);
        bool isEnabled();
        bool isConnected();
        uint8_t getStatus();
        void startOTAUpdate(String firmwareUrl);
        bool isOTAUpdateRunning();
        void onControlCommand(WifiControlCommandCallback callback);

    private:
        void setupRoutes();
        void startServer();

        void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
        void onWifiConnected(WiFiEvent_t event, WiFiEventInfo_t info);
        void onWifiGotIp(WiFiEvent_t event, WiFiEventInfo_t info);

        void handleGetState(AsyncWebServerRequest *request);
        void handlePostState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
        void handleGetInfo(AsyncWebServerRequest *request);
        void handlePostWifi(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

        Config *config;
        Floower *floower;
        AsyncWebServer *server = nullptr;
        WifiControlCommandCallback controlCommandCallback;

        bool enabled = false;
        bool wifiOn = false;
        bool wifiConnected = false;
        bool wifiFailed = false;
        bool serverRunning = false;

        wifi_event_id_t wifiConnectedEventId;
        wifi_event_id_t wifiGotIpEventId;
        wifi_event_id_t wifiDisconnectedEventId;

        unsigned long reconnectTime = 0;
};
