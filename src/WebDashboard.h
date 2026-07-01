#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "SystemState.h"
#include "ConfigManager.h"
#include "Fingerprint.h"

class WebDashboard {
public:
    WebDashboard(ConfigManager& config, FingerprintManager& fingerprint);
    void begin();
    void handleClient();

private:
    WebServer _server;
    ConfigManager& _config;
    FingerprintManager& _fingerprint;
    bool _updateSuccess = false;
    bool authenticate();

    void handleRoot();
    void handleApiStatus();
    void handleApiConfigGet();
    void handleApiConfigPost();
    
    // Handlers mapped from s3gps Web UI
    void handleApiToggleAlarm();
    void handleApiLogs();
    void handleApiFingerStatus();
    void handleApiFingerAdd();
    void handleApiFingerVerify();
    void handleApiFingerDelete();
    void handleApiFingerClearAll();
    void handleUpdateUpload();
    void handleUpdateFinished();
};

#endif // WEB_DASHBOARD_H
