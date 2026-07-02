#include "WebDashboard.h"
#include "WebHTML.h"
#include <ArduinoJson.h>
#include <Update.h>

WebDashboard::WebDashboard(ConfigManager& config, FingerprintManager& fingerprint)
    : _server(80), _config(config), _fingerprint(fingerprint) {}

void WebDashboard::begin() {
    Serial.println("Starting WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(state.apSSID.c_str(), state.apPass.c_str());
    
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());

    _server.on("/", HTTP_GET, std::bind(&WebDashboard::handleRoot, this));
    _server.on("/api/status", HTTP_GET, std::bind(&WebDashboard::handleApiStatus, this));
    
    // s3gps uses /api/config for GET and /api/save for POST
    _server.on("/api/config", HTTP_GET, std::bind(&WebDashboard::handleApiConfigGet, this));
    _server.on("/api/save", HTTP_POST, std::bind(&WebDashboard::handleApiConfigPost, this));
    _server.on("/api/config", HTTP_POST, std::bind(&WebDashboard::handleApiConfigPost, this)); // Fallback
    
    _server.on("/api/toggle_alarm", HTTP_POST, std::bind(&WebDashboard::handleApiToggleAlarm, this));
    _server.on("/api/logs", HTTP_GET, std::bind(&WebDashboard::handleApiLogs, this));
    
    _server.on("/api/finger/status", HTTP_GET, std::bind(&WebDashboard::handleApiFingerStatus, this));
    _server.on("/api/finger/add", HTTP_POST, std::bind(&WebDashboard::handleApiFingerAdd, this));
    _server.on("/api/finger/verify", HTTP_POST, std::bind(&WebDashboard::handleApiFingerVerify, this));
    _server.on("/api/finger/delete", HTTP_POST, std::bind(&WebDashboard::handleApiFingerDelete, this));
    _server.on("/api/finger/clearall", HTTP_POST, std::bind(&WebDashboard::handleApiFingerClearAll, this));
    // FIX-CRIT-08: The UI exposed FOTA but the backend route did not exist.
    _server.on("/update", HTTP_POST,
               std::bind(&WebDashboard::handleUpdateFinished, this),
               std::bind(&WebDashboard::handleUpdateUpload, this));
    
    _server.begin();
    Serial.println("HTTP server started");
}

void WebDashboard::handleClient() {
    _server.handleClient();
}

bool WebDashboard::authenticate() {
    // FIX-CRIT-08: Protect configuration, biometric operations and FOTA with
    // HTTP Basic auth (user admin, password equals the AP password).
    String password;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        password = state.apPass;
        xSemaphoreGive(stateMutex);
    }
    if (_server.authenticate("admin", password.c_str())) return true;
    _server.requestAuthentication(BASIC_AUTH, "S3 GPS Tracker");
    return false;
}

void WebDashboard::handleRoot() {
    if (!authenticate()) return;
    _server.send(200, "text/html", index_html);
}

void WebDashboard::handleApiStatus() {
    if (!authenticate()) return;
    JsonDocument doc;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Build state integer similar to s3gps
        int s = 0;
        if (!state.networkConnected) s = 3; // NO_NET
        else if (!state.tcpConnected) s = 4; // NO_INET
        else if (!state.gps.gpsFixValid) s = 5; // NO_GPS
        else s = 6; // HAS_GPS
        
        doc["state"] = s;
        doc["terminal_id"] = state.imei.length() >= 12 ? state.imei.substring(state.imei.length() - 12) : "N/A";
        doc["imei"] = state.imei;
        doc["ccid"] = state.ccid;
        doc["lat"] = state.gps.latitude;
        doc["lon"] = state.gps.longitude;
        
        char tbuf[32];
        time_t tnow = state.gps.utcTime / 1000ULL;
        if (tnow > 1000000000) {
            time_t local_tnow = tnow + 7 * 3600; 
            struct tm tm_local_buf; gmtime_r(&local_tnow, &tm_local_buf);
            sprintf(tbuf, "%02d/%02d/20%02d %02d:%02d:%02d", tm_local_buf.tm_mday, tm_local_buf.tm_mon + 1, tm_local_buf.tm_year % 100, tm_local_buf.tm_hour, tm_local_buf.tm_min, tm_local_buf.tm_sec);
        } else {
            strcpy(tbuf, "N/A");
        }
        doc["time"] = tbuf;
        
        doc["speed"] = state.gps.speed;
        doc["course"] = state.gps.course;
        doc["vbat"] = state.vbatVoltage;
        const bool overspeed = state.gps.speed > state.overspeedLimit;
        doc["overspeed_limit"] = state.overspeedLimit;

        doc["alarm_sos"] = state.alarmSos;
        doc["alarm_overspeed"] = overspeed;
        doc["alarm_fatigue"] = state.alarmFatigue;
        doc["alarm_gps_ant"] = state.alarmGpsAntenna;
        doc["alarm_power_cut"] = state.alarmPowerCut;
        doc["alarm_collision"] = state.alarmCollision;
        doc["status_acc"] = state.accState;
        doc["use_phys_acc"] = state.usePhysicalAcc;
        
        uint32_t alarm = 0;
        if (state.alarmSos) alarm |= (1UL << 0);
        if (overspeed) alarm |= (1UL << 1);
        if (state.alarmFatigue) alarm |= (1UL << 2);
        if (state.alarmGpsAntenna) alarm |= (1UL << 5);
        if (state.alarmPowerCut) alarm |= (1UL << 8);
        if (state.alarmCollision) alarm |= (1UL << 20);
        
        uint32_t status = 0;
        if (state.accState) status |= (1 << 0);
        if (state.gps.gpsFixValid) status |= (1 << 1);
        if (state.gps.latitude < 0) status |= (1 << 2);
        if (state.gps.longitude < 0) status |= (1 << 3);
        if (state.gps.speed > 2.0) status |= (1 << 10);
        else status |= (1 << 11);
        
        char alarmHex[12];
        char statusHex[12];
        sprintf(alarmHex, "%08X", alarm);
        sprintf(statusHex, "%08X", status);
        
        doc["alarm_hex"] = alarmHex;
        doc["status_hex"] = statusHex;
        doc["heap"] = state.freeHeap;
        xSemaphoreGive(stateMutex);
    }

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WebDashboard::handleApiConfigGet() {
    if (!authenticate()) return;
    JsonDocument doc;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        doc["ssid"] = state.apSSID;
        // Never echo the WiFi password back to every connected browser.
        doc["pass"] = "";
        doc["ip"] = state.serverHost;
        doc["port"] = state.serverPort;
        doc["interval"] = state.intervalSec;
        doc["overspeed"] = state.overspeedLimit;
        xSemaphoreGive(stateMutex);
    }

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WebDashboard::handleApiConfigPost() {
    if (!authenticate()) return;
    bool hasUpdate = false;
    const String ssid = _server.arg("ssid");
    const String pass = _server.arg("pass");
    const String host = _server.arg("ip");
    const int port = _server.arg("port").toInt();
    const int interval = _server.arg("interval").toInt();
    const int overspeed = _server.arg("overspeed").toInt();

    bool hostValid = !host.isEmpty() && host.length() <= 128;
    for (size_t i = 0; i < host.length() && hostValid; ++i) {
        const char c = host[i];
        hostValid = isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':';
    }
    if (ssid.isEmpty() || ssid.length() > 32 || (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) ||
        !hostValid || port < 1 || port > 65535 || interval < 5 || interval > 3600 ||
        overspeed < 10 || overspeed > 200) {
        _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"invalid config\"}");
        return;
    }

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state.apSSID = ssid;
        if (!pass.isEmpty()) state.apPass = pass;
        state.serverHost = host;
        state.serverPort = port;
        state.intervalSec = interval;
        state.overspeedLimit = overspeed;
        hasUpdate = true;
        xSemaphoreGive(stateMutex);
    }

    if (hasUpdate) {
        _config.save();
    }
    
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
}

void WebDashboard::handleApiToggleAlarm() {
    if (!authenticate()) return;
    if (!_server.hasArg("name") || !_server.hasArg("value")) {
        _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"missing name/value\"}");
        return;
    }

    const String name = _server.arg("name");
    const bool enabled = _server.arg("value") == "1";
    bool recognized = true;
    bool conflict = false;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        _server.send(503, "application/json", "{\"ok\":false,\"msg\":\"state busy\"}");
        return;
    }

    if (name == "acc_phys") {
        state.usePhysicalAcc = enabled;
        state.accState = enabled ? state.physicalAccState : state.virtualAccState;
    } else if (name == "acc") {
        if (state.usePhysicalAcc) {
            conflict = true;
        } else {
            state.virtualAccState = enabled;
            state.accState = enabled;
        }
    } else if (name == "sos") {
        state.alarmSos = enabled;
    } else if (name == "fatigue") {
        state.alarmFatigue = enabled;
    } else if (name == "gps_ant") {
        state.alarmGpsAntenna = enabled;
    } else if (name == "power_cut") {
        state.alarmPowerCut = enabled;
    } else if (name == "collision") {
        state.alarmCollision = enabled;
    } else {
        recognized = false;
    }
    xSemaphoreGive(stateMutex);

    if (!recognized) {
        _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"unknown alarm\"}");
    } else if (conflict) {
        _server.send(409, "application/json", "{\"ok\":false,\"msg\":\"ACC is controlled by physical input\"}");
    } else {
        _config.save();
        _server.send(200, "application/json", "{\"ok\":true}");
    }
}

void WebDashboard::handleApiLogs() {
    if (!authenticate()) return;
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    
    if (webLogsMutex && xSemaphoreTake(webLogsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (const auto& log : webLogs) {
            array.add(log);
        }
        xSemaphoreGive(webLogsMutex);
    }
    
    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WebDashboard::handleApiFingerStatus() {
    if (!authenticate()) return;
    JsonDocument doc;
    doc["sensorReady"] = _fingerprint.isReady();
    doc["fingerCount"] = _fingerprint.getCachedUserCount();
    doc["state"] = static_cast<int>(_fingerprint.getState());
    doc["lastResult"] = _fingerprint.getLastResult();
    doc["lastResultType"] = _fingerprint.getLastResultType();
    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WebDashboard::handleApiFingerAdd() {
    if (!authenticate()) return;
    const bool ok = _fingerprint.startAdding();
    _server.send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"sensor busy/offline\"}");
}

void WebDashboard::handleApiFingerVerify() {
    if (!authenticate()) return;
    const bool ok = _fingerprint.startVerifying();
    _server.send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"sensor busy/offline\"}");
}

void WebDashboard::handleApiFingerDelete() {
    if (!authenticate()) return;
    const int id = _server.arg("id").toInt();
    const bool ok = id >= 1 && id <= 4095 && _fingerprint.startDeleting(id);
    _server.send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"invalid id or sensor busy\"}");
}

void WebDashboard::handleApiFingerClearAll() {
    if (!authenticate()) return;
    const bool ok = _fingerprint.startClearAll();
    _server.send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"sensor busy/offline\"}");
}

void WebDashboard::handleUpdateUpload() {
    if (!authenticate()) {
        Update.abort();
        _updateSuccess = false;
        return;
    }
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        _updateSuccess = Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE && _updateSuccess) {
        _updateSuccess = Update.write(upload.buf, upload.currentSize) == upload.currentSize;
    } else if (upload.status == UPLOAD_FILE_END && _updateSuccess) {
        _updateSuccess = Update.end(true);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        _updateSuccess = false;
    }
}

void WebDashboard::handleUpdateFinished() {
    if (!authenticate()) return;
    _server.send(_updateSuccess ? 200 : 500, "text/plain", _updateSuccess ? "OK" : "FAIL");
    if (_updateSuccess) {
        vTaskDelay(pdMS_TO_TICKS(250));
        ESP.restart();
    }
}
