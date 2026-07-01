#include "ConfigManager.h"

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
    _prefsMutex = xSemaphoreCreateMutex();
    _prefs.begin("s3gps", false);
    load();
}

void ConfigManager::load() {
    state.serverHost = _prefs.getString("serverHost", DEFAULT_SERVER_HOST);
    state.serverPort = _prefs.getUShort("serverPort", DEFAULT_SERVER_PORT);
    state.intervalSec = _prefs.getUInt("intervalSec", DEFAULT_INTERVAL_SEC);
    state.sampleIntervalSec = _prefs.getUShort("sampleSec", DEFAULT_GPS_SAMPLE_INTERVAL_SEC);
    state.batchSize = _prefs.getUChar("batchSize", DEFAULT_BATCH_SIZE);
    state.apSSID = _prefs.getString("apSSID", DEFAULT_AP_SSID);
    state.apPass = _prefs.getString("apPass", DEFAULT_AP_PASS);
    state.overspeedLimit = _prefs.getUShort("overspeed", DEFAULT_OVERSPEED_KPH);
    state.usePhysicalAcc = _prefs.getBool("usePhysAcc", true);
    state.virtualAccState = _prefs.getBool("virtualAcc", false);
    state.alarmSos = _prefs.getBool("alarmSos", false);
    state.alarmFatigue = _prefs.getBool("alarmFatigue", false);
    state.alarmGpsAntenna = _prefs.getBool("alarmGpsAnt", false);
    state.alarmPowerCut = _prefs.getBool("alarmPower", false);
    state.alarmCollision = _prefs.getBool("alarmColl", false);
    state.totalMileage = _prefs.getUInt("mileage", 0);

    // FIX-CRIT-08: Reject corrupt/legacy NVS values before they can cause a
    // tight flash-write loop or an invalid AT command.
    if (state.intervalSec < 5 || state.intervalSec > 3600) state.intervalSec = DEFAULT_INTERVAL_SEC;
    if (state.sampleIntervalSec < 5 || state.sampleIntervalSec > 300) {
        state.sampleIntervalSec = DEFAULT_GPS_SAMPLE_INTERVAL_SEC;
    }
    if (state.batchSize < 1 || state.batchSize > 20) state.batchSize = DEFAULT_BATCH_SIZE;
    if (state.serverPort == 0) state.serverPort = DEFAULT_SERVER_PORT;
    if (state.serverHost.isEmpty() || state.serverHost.length() > 128) state.serverHost = DEFAULT_SERVER_HOST;
    if (state.apSSID.isEmpty() || state.apSSID.length() > 32) state.apSSID = DEFAULT_AP_SSID;
    if (state.apPass.length() < 8 || state.apPass.length() > 63) state.apPass = DEFAULT_AP_PASS;
    if (state.overspeedLimit < 10 || state.overspeedLimit > 200) state.overspeedLimit = DEFAULT_OVERSPEED_KPH;
    
    const bool hasStoredLocation = _prefs.isKey("lastLat") && _prefs.isKey("lastLon");
    const float storedLat = _prefs.getFloat("lastLat", 0.0f);
    const float storedLon = _prefs.getFloat("lastLon", 0.0f);
    const bool storedLocationValid = hasStoredLocation && isfinite(storedLat) && isfinite(storedLon) &&
                                     storedLat >= -90.0f && storedLat <= 90.0f &&
                                     storedLon >= -180.0f && storedLon <= 180.0f &&
                                     !(storedLat == 0.0f && storedLon == 0.0f);
    state.gps.latitude = storedLocationValid ? storedLat : 0.0f;
    state.gps.longitude = storedLocationValid ? storedLon : 0.0f;
    state.hasLastKnownLocation = storedLocationValid;
}

void ConfigManager::save() {
    String serverHost, apSSID, apPass;
    uint16_t serverPort;
    uint32_t intervalSec;
    uint16_t sampleIntervalSec;
    uint8_t batchSize;
    uint16_t overspeedLimit;
    bool usePhysicalAcc, virtualAccState;
    bool alarmSos, alarmFatigue, alarmGpsAntenna, alarmPowerCut, alarmCollision;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        serverHost = state.serverHost;
        serverPort = state.serverPort;
        intervalSec = state.intervalSec;
        sampleIntervalSec = state.sampleIntervalSec;
        batchSize = state.batchSize;
        overspeedLimit = state.overspeedLimit;
        usePhysicalAcc = state.usePhysicalAcc;
        virtualAccState = state.virtualAccState;
        alarmSos = state.alarmSos;
        alarmFatigue = state.alarmFatigue;
        alarmGpsAntenna = state.alarmGpsAntenna;
        alarmPowerCut = state.alarmPowerCut;
        alarmCollision = state.alarmCollision;
        apSSID = state.apSSID;
        apPass = state.apPass;
        xSemaphoreGive(stateMutex);
    } else {
        return;
    }

    // FIX-CRIT-06: Never hold stateMutex while writing Preferences. A separate
    // mutex serializes Web and Network NVS writes without recursive locking.
    if (_prefsMutex && xSemaphoreTake(_prefsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        _prefs.putString("serverHost", serverHost);
        _prefs.putUShort("serverPort", serverPort);
        _prefs.putUInt("intervalSec", intervalSec);
        _prefs.putUShort("sampleSec", sampleIntervalSec);
        _prefs.putUChar("batchSize", batchSize);
        _prefs.putUShort("overspeed", overspeedLimit);
        _prefs.putBool("usePhysAcc", usePhysicalAcc);
        _prefs.putBool("virtualAcc", virtualAccState);
        _prefs.putBool("alarmSos", alarmSos);
        _prefs.putBool("alarmFatigue", alarmFatigue);
        _prefs.putBool("alarmGpsAnt", alarmGpsAntenna);
        _prefs.putBool("alarmPower", alarmPowerCut);
        _prefs.putBool("alarmColl", alarmCollision);
        _prefs.putString("apSSID", apSSID);
        _prefs.putString("apPass", apPass);
        xSemaphoreGive(_prefsMutex);
    }
}

void ConfigManager::saveMileage(uint32_t mileageMeters) {
    if (_prefsMutex && xSemaphoreTake(_prefsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        _prefs.putUInt("mileage", mileageMeters);
        xSemaphoreGive(_prefsMutex);
    }
}

void ConfigManager::saveLocation(float lat, float lon) {
    // FIX-CRIT-06: lat/lon are value parameters; taking stateMutex here caused
    // a recursive-lock timeout when called by the GPS update path.
    if (_prefsMutex && xSemaphoreTake(_prefsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        _prefs.putFloat("lastLat", lat);
        _prefs.putFloat("lastLon", lon);
        xSemaphoreGive(_prefsMutex);
    }
}

void ConfigManager::factoryReset() {
    // FIX-CRIT-08: The old implementation saved current values and therefore
    // was not a factory reset at all.
    if (_prefsMutex && xSemaphoreTake(_prefsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        _prefs.clear();
        xSemaphoreGive(_prefsMutex);
    }
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state.serverHost = DEFAULT_SERVER_HOST;
        state.serverPort = DEFAULT_SERVER_PORT;
        state.intervalSec = DEFAULT_INTERVAL_SEC;
        state.sampleIntervalSec = DEFAULT_GPS_SAMPLE_INTERVAL_SEC;
        state.batchSize = DEFAULT_BATCH_SIZE;
        state.overspeedLimit = DEFAULT_OVERSPEED_KPH;
        state.apSSID = DEFAULT_AP_SSID;
        state.apPass = DEFAULT_AP_PASS;
        state.gps.latitude = 0.0f;
        state.gps.longitude = 0.0f;
        state.hasLastKnownLocation = false;
        state.totalMileage = 0;
        state.usePhysicalAcc = true;
        state.virtualAccState = false;
        state.alarmSos = false;
        state.alarmFatigue = false;
        state.alarmGpsAntenna = false;
        state.alarmPowerCut = false;
        state.alarmCollision = false;
        xSemaphoreGive(stateMutex);
    }
}
