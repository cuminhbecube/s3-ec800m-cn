#include <Arduino.h>
#include <FreeRTOS.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <esp_arduino_version.h>
#include "config.h"
#include "SystemState.h"
#include "ConfigManager.h"
#include "WebDashboard.h"
#include "EC800.h"
#include "Codec8E.h"
#include "BacklogManager.h"
#include "Fingerprint.h"
#include "Indicators.h"

// Global Instances
SystemState state;
SemaphoreHandle_t stateMutex = NULL;
std::vector<String> webLogs;
SemaphoreHandle_t webLogsMutex = NULL;

ConfigManager configManager;
FingerprintManager fingerprint(SERIAL_FINGERPRINT, FINGERPRINT_WAKE_PIN);
WebDashboard dashboard(configManager, fingerprint);
EC800 ec800(SERIAL_EC800, EC800_PWRKEY_PIN, EC800_RST_PIN);
BacklogManager backlog;
Indicators indicators(NEOPIXEL_PIN, BUZZER_PIN, LED_STATUS_PIN);

// WiFi Auto-Off state
bool wifiEnabled = true;
uint32_t lastWifiClientTime = 0;

// Task Handles
TaskHandle_t NetworkTaskHandle;
TaskHandle_t SensorsTaskHandle;
TaskHandle_t IndicatorsTaskHandle;
TaskHandle_t FingerprintTaskHandle;

// Callbacks
void onFingerprintMatch(int id) {
    Serial.printf("Fingerprint Matched: ID #%d\n", id);
    indicators.beep(3, 100);
}

void onFingerprintNoMatch() {
    Serial.println("Fingerprint Not Matched");
    indicators.beep(1, 500); // long beep
}

// Helper: Read VBAT
void readVBAT() {
    int raw = analogRead(VBAT_ADC_PIN);
    // Hardware has a 1:11 voltage divider. 
    // Raw ADC ~455 corresponds to ~0.366V at the pin, which is 4.21V at the battery.
    // Multiplier = 4.21 / (455 / 4095.0 * 3.3) = 11.48
    float voltage = (raw / 4095.0) * 3.3 * 11.48;
    
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        state.vbatVoltage = (state.vbatVoltage == 0.0) ? voltage : (state.vbatVoltage * 0.9 + voltage * 0.1);
        int pct = (state.vbatVoltage - 3.3) / (4.2 - 3.3) * 100;
        if (pct > 100) pct = 100;
        if (pct < 0) pct = 0;
        state.vbatPercent = pct;
        xSemaphoreGive(stateMutex);
    }
}

// Helper: Add Web Log
void addWebLog(const String& message) {
    if (webLogsMutex && xSemaphoreTake(webLogsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        char ts[32];
        uint64_t utcTime = 0;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            utcTime = state.gps.utcTime;
            xSemaphoreGive(stateMutex);
        }
        time_t tnow = utcTime / 1000ULL;
        if (tnow > 1000000000) {
            time_t local_tnow = tnow + 7 * 3600; 
            struct tm tm_local_buf; gmtime_r(&local_tnow, &tm_local_buf);
            sprintf(ts, "[%02d:%02d:%02d] ", tm_local_buf.tm_hour, tm_local_buf.tm_min, tm_local_buf.tm_sec);
        } else {
            sprintf(ts, "[%lu] ", millis());
        }
        webLogs.push_back(String(ts) + message);
        if (webLogs.size() > 50) {
            webLogs.erase(webLogs.begin());
        }
        xSemaphoreGive(webLogsMutex);
    }
}

// Helper: Sync System Time from Network
void syncSystemTime() {
    int yr, mo, dy, hr, mi, se;
    bool has_time = false;
    time_t epoch = 0;
    
    setenv("TZ", "UTC0", 1);
    tzset();

    Serial.println("Syncing time from GNSS...");
    if (ec800.getGNSSTime(yr, mo, dy, hr, mi, se)) {
        has_time = true;
        struct tm t = {0};
        t.tm_year = yr + 2000 - 1900;
        t.tm_mon = mo - 1;
        t.tm_mday = dy;
        t.tm_hour = hr;
        t.tm_min = mi;
        t.tm_sec = se;
        epoch = mktime(&t);
        Serial.println("Got GNSS Time (UTC)!");
    } else {
        Serial.println("No GNSS, trying NTP...");
        const char* servers[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
        for (int i = 0; i < 3; i++) {
            Serial.printf("Trying NTP: %s\n", servers[i]);
            if (ec800.syncNTP(String(servers[i]), yr, mo, dy, hr, mi, se)) {
                has_time = true;
                struct tm t = {0};
                t.tm_year = yr + 2000 - 1900;
                t.tm_mon = mo - 1;
                t.tm_mday = dy;
                t.tm_hour = hr;
                t.tm_min = mi;
                t.tm_sec = se;
                epoch = mktime(&t);
                Serial.println("NTP Sync Success!");
                break;
            }
        }
    }
    
    if (has_time) {
        struct timeval tv;
        tv.tv_sec = epoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        
        setenv("TZ", "UTC-7", 1);
        tzset();
        Serial.println("System time synchronized to (UTC+7)!");
    } else {
        Serial.println("Time sync failed!");
    }
}

static double distanceMeters(float lat1, float lon1, float lat2, float lon2) {
    constexpr double DEG_TO_RAD_D = 0.017453292519943295;
    constexpr double EARTH_RADIUS_M = 6371000.0;
    const double p1 = lat1 * DEG_TO_RAD_D;
    const double p2 = lat2 * DEG_TO_RAD_D;
    const double dp = (lat2 - lat1) * DEG_TO_RAD_D;
    const double dl = (lon2 - lon1) * DEG_TO_RAD_D;
    const double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return EARTH_RADIUS_M * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// FIX-CRIT-01/03/05/06: GPS acquisition depends on modem/GNSS readiness,
// never on TCP readiness. Task_Network remains the sole EC800 UART owner.
static void pollGPS() {
    float lat = 0, lon = 0, speed = 0, course = 0, alt = 0, pdop = 0, hdop = 0;
    uint8_t sats = 0;
    uint64_t utcTime = 0;
    bool isValid = false;
    const bool gpsOk = ec800.parseGPS(lat, lon, speed, course, alt, sats, pdop, hdop, utcTime, isValid);
    const int rssi = ec800.getRSSI();

    static bool persistenceInitialized = false;
    static float lastSavedLat = 0.0f, lastSavedLon = 0.0f;
    static uint32_t lastSavedTime = 0;
    static bool odometerInitialized = false;
    static float odometerLat = 0.0f, odometerLon = 0.0f;
    static bool previousGpsValid = false;
    static uint32_t lastGpsFixBeep = 0;
    static bool mileagePersistenceInitialized = false;
    static uint32_t lastSavedMileage = 0;
    static uint32_t lastMileageSaveTime = 0;
    bool shouldSaveLocation = false;
    bool gpsFixAcquired = false;
    bool shouldSaveMileage = false;
    uint32_t mileageToSave = 0;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    state.rssi = rssi;
    if (!mileagePersistenceInitialized) {
        lastSavedMileage = state.totalMileage;
        mileagePersistenceInitialized = true;
    }
    if (!persistenceInitialized && state.hasLastKnownLocation) {
        lastSavedLat = state.gps.latitude;
        lastSavedLon = state.gps.longitude;
        persistenceInitialized = true;
    }

    if (gpsOk && isValid) {
        if (speed < 2.0f && state.gps.isValid) {
            lat = state.gps.latitude;
            lon = state.gps.longitude;
            course = state.gps.course;
            speed = 0.0f;
        }
        if (odometerInitialized) {
            const double delta = distanceMeters(odometerLat, odometerLon, lat, lon);
            if (delta >= 2.0 && delta <= 1000.0) state.totalMileage += static_cast<uint32_t>(lround(delta));
        }
        odometerLat = lat;
        odometerLon = lon;
        odometerInitialized = true;

        state.gps.latitude = lat;
        state.gps.longitude = lon;
        state.gps.speed = speed;
        state.gps.course = course;
        state.gps.altitude = alt;
        state.gps.satellites = sats;
        state.gps.pdop = pdop;
        state.gps.hdop = hdop;
        state.gps.utcTime = utcTime;
        state.gps.isValid = true;
        state.hasLastKnownLocation = true;
        gpsFixAcquired = !previousGpsValid &&
                         (lastGpsFixBeep == 0 || millis() - lastGpsFixBeep >= 60000);
        if (gpsFixAcquired) lastGpsFixBeep = millis();
        previousGpsValid = true;

        if (state.totalMileage != lastSavedMileage &&
            (state.totalMileage - lastSavedMileage >= 1000 || millis() - lastMileageSaveTime >= 600000)) {
            mileageToSave = state.totalMileage;
            lastSavedMileage = mileageToSave;
            lastMileageSaveTime = millis();
            shouldSaveMileage = true;
        }

        shouldSaveLocation = millis() - lastSavedTime >= 300000 ||
                             fabsf(lat - lastSavedLat) > 0.001f || fabsf(lon - lastSavedLon) > 0.001f;
        if (shouldSaveLocation) {
            lastSavedLat = lat;
            lastSavedLon = lon;
            lastSavedTime = millis();
        }
    } else {
        const time_t now = time(nullptr);
        if (now > 1000000000) state.gps.utcTime = static_cast<uint64_t>(now) * 1000ULL;
        state.gps.speed = 0.0f;
        state.gps.course = 0.0f;
        state.gps.altitude = 0.0f;
        state.gps.satellites = 0;
        state.gps.pdop = 0.0f;
        state.gps.hdop = 0.0f;
        state.gps.isValid = false;
        previousGpsValid = false;
        odometerInitialized = false;
    }
    xSemaphoreGive(stateMutex);

    if (shouldSaveLocation) configManager.saveLocation(lat, lon);
    if (shouldSaveMileage) configManager.saveMileage(mileageToSave);
    if (gpsFixAcquired) {
        addWebLog("GNSS fix acquired");
        indicators.beep(1, 120);
    }
}

static bool isSafeHost(const String& host) {
    if (host.isEmpty() || host.length() > 128) return false;
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) return false;
    }
    return true;
}

static String formatUtcTimestamp(uint64_t utcTimeMs) {
    if (utcTimeMs < 1000000000000ULL) return "unknown";
    const time_t seconds = static_cast<time_t>(utcTimeMs / 1000ULL);
    struct tm utc{};
    gmtime_r(&seconds, &utc);
    char text[24];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &utc);
    return String(text);
}

// Network Task
void Task_Network(void *pvParameters) {
    int init_step = 0;
    unsigned long action_timer = 0;
    int sim_err_cnt = 0;
    int net_err_cnt = 0;
    int modem_reset_cnt = 0; // Watchdog: count consecutive modem resets
    bool is_ready_to_send = false;
    uint32_t lastGpsRead = 0;
    uint32_t lastBatchSend = millis();
    bool gpsEnabled = false;
    uint32_t factoryResetCode = 0;
    uint32_t factoryResetCodeExpires = 0;
    
    while (true) {
        unsigned long now = millis();

        bool accOnForPolling = true;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            accOnForPolling = state.accState;
            xSemaphoreGive(stateMutex);
        }
        const uint32_t gpsPollInterval = accOnForPolling ? 5000UL : POWER_SAVE_INTERVAL_SEC * 1000UL;
        if (gpsEnabled && millis() - lastGpsRead >= gpsPollInterval) {
            pollGPS();
            lastGpsRead = millis();
        }
        
        if (init_step == 0) {
            gpsEnabled = false;
            is_ready_to_send = false;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                state.networkConnected = false;
                state.tcpConnected = false;
                xSemaphoreGive(stateMutex);
            }
            // Watchdog: If modem has been reset too many times, reboot ESP
            modem_reset_cnt++;
            if (modem_reset_cnt > 3) {
                Serial.println("WATCHDOG: Modem failed after 3 resets. Rebooting ESP...");
                addWebLog("WATCHDOG: ESP Restart");
                vTaskDelay(pdMS_TO_TICKS(500));
                ESP.restart();
            }
            Serial.printf("Hardware reset EC800 (attempt %d/3)...\n", modem_reset_cnt);
            // Hardware reset EC800
            pinMode(EC800_PWRKEY_PIN, OUTPUT);
            pinMode(EC800_RST_PIN, OUTPUT);
            digitalWrite(EC800_RST_PIN, HIGH);
            action_timer = now;
            init_step++;
        } else if (init_step == 1) {
            if (now - action_timer >= 300) {
                digitalWrite(EC800_RST_PIN, LOW);
                action_timer = now;
                init_step++;
            }
        } else if (init_step == 2) {
            if (now - action_timer >= 500) {
                digitalWrite(EC800_PWRKEY_PIN, HIGH);
                action_timer = now;
                init_step++;
            }
        } else if (init_step == 3) {
            if (now - action_timer >= 3000) {
                Serial.println("Initializing EC800...");
                ec800.begin(115200, EC800_RX_PIN, EC800_TX_PIN);
                if (!ec800.init()) {
                    Serial.println("EC800 Init Failed! Retrying in 3s...");
                    action_timer = now;
                } else {
                    init_step++;
                }
            }
        } else if (init_step == 4) {
            if (now - action_timer >= 2000) {
                String imei;
                if (!ec800.getIMEI(imei)) {
                    sim_err_cnt++;
                    Serial.println("Failed to read IMEI. Retrying...");
                    if (sim_err_cnt >= 3) {
                        Serial.println("EC800 unresponsive or no SIM. Resetting Hardware...");
                        sim_err_cnt = 0;
                        init_step = 0;
                    }
                    action_timer = now;
                } else {
                    String ccid;
                    if (ec800.getCCID(ccid)) {
                        Serial.println("EC800 CCID: " + ccid);
                    } else {
                        ccid = "Unknown";
                    }
                    
                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        state.imei = imei;
                        state.ccid = ccid;
                        xSemaphoreGive(stateMutex);
                    }
                    Serial.println("EC800 IMEI: " + imei);
                    gpsEnabled = ec800.enableGPS();
                    // Poll once immediately even when booting into power save;
                    // subsequent ACC-OFF polls use the reduced 300 s cadence.
                    lastGpsRead = millis() - POWER_SAVE_INTERVAL_SEC * 1000UL;
                    sim_err_cnt = 0;
                    init_step++;
                }
            }
        } else if (init_step == 5) {
            if (now - action_timer >= 3000) {
                if (!ec800.isNetworkRegistered()) {
                    Serial.println("Waiting for network...");
                    net_err_cnt++;
                    if (net_err_cnt >= 10) { // ~30s
                        Serial.println("Network timeout. Resetting Hardware...");
                        net_err_cnt = 0;
                        init_step = 0;
                    }
                    action_timer = now;
                } else {
                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        state.networkConnected = true;
                        xSemaphoreGive(stateMutex);
                    }
                    Serial.println("Network Registered!");
                    syncSystemTime();
                    net_err_cnt = 0;
                    init_step++;
                }
            }
        } else if (init_step == 6) {
            if (now - action_timer >= 3000) {
                String host;
                uint16_t port;
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    host = state.serverHost;
                    port = state.serverPort;
                    xSemaphoreGive(stateMutex);
                }
                
                Serial.printf("Connecting to TCP: %s:%d...\n", host.c_str(), port);
                if (ec800.connectTCP(host, port)) {
                    Serial.println("TCP Connected! Sending Login...");
                    String imei_copy;
                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        imei_copy = state.imei;
                        xSemaphoreGive(stateMutex);
                    }
                    auto loginPacket = Codec8E::buildLoginPacket(imei_copy);
                    if (ec800.sendTCP(loginPacket)) {
                        addWebLog("TX: Login Packet");
                        std::vector<uint8_t> ack;
                        if (ec800.readTCP(ack, 5000, 1) && ack.size() == 1 && ack[0] == 0x01) {
                            Serial.println("Codec8E Login Accepted!");
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                state.tcpConnected = true;
                                xSemaphoreGive(stateMutex);
                            }
                            is_ready_to_send = true;
                            modem_reset_cnt = 0; // Watchdog: modem is working
                            init_step++;
                        } else {
                            Serial.println("Codec8E Login Rejected/Timeout.");
                            action_timer = now;
                        }
                    } else {
                        Serial.println("Failed to send login packet.");
                        action_timer = now;
                    }
                } else {
                    Serial.println("TCP Connect Failed!");
                    net_err_cnt++;
                    if (net_err_cnt >= 5) {
                        init_step = 0;
                    }
                    action_timer = now;
                }
            }
        } else {
            // Main connected state (init_step == 7)

            bool reconnectRequested = false;
            bool gnssRestartRequested = false;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                reconnectRequested = state.reconnectRequested;
                gnssRestartRequested = state.gnssRestartRequested;
                state.reconnectRequested = false;
                state.gnssRestartRequested = false;
                xSemaphoreGive(stateMutex);
            }
            if (reconnectRequested) {
                Serial.println("Reconnect requested; preserving backlog and reopening Traccar session");
                addWebLog("CMD: reconnecting Traccar");
                is_ready_to_send = false;
                ec800.closeTCP();
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    state.tcpConnected = false;
                    xSemaphoreGive(stateMutex);
                }
                init_step = 6;
                action_timer = millis() - 3000;
                continue;
            }
            if (gnssRestartRequested) {
                Serial.println("GNSS restarting");
                gpsEnabled = ec800.restartGPS();
                lastGpsRead = millis() - POWER_SAVE_INTERVAL_SEC * 1000UL;
                Serial.println(gpsEnabled ? "GNSS ready" : "GNSS restart failed");
                addWebLog(gpsEnabled ? "GNSS restart: ready" : "GNSS restart: failed");
            }
            
            // Process queue
            const uint32_t count = backlog.getRecordCount();
            uint32_t sendIntervalSec = DEFAULT_INTERVAL_SEC;
            uint8_t batchSize = DEFAULT_BATCH_SIZE;
            bool accOn = true;
            bool forceSend = false;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                sendIntervalSec = state.intervalSec;
                batchSize = state.batchSize;
                accOn = state.accState;
                forceSend = state.forceSendRequested;
                xSemaphoreGive(stateMutex);
            }
            const uint32_t activeSendIntervalSec = accOn ? sendIntervalSec : POWER_SAVE_INTERVAL_SEC;
            const bool batchFull = accOn && count >= batchSize;
            const bool sendTimeout = count > 0 &&
                    millis() - lastBatchSend >= static_cast<uint64_t>(activeSendIntervalSec) * 1000ULL;

            if (count > 0 && is_ready_to_send && (forceSend || batchFull || sendTimeout)) {
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    state.uploadingBacklog = true;
                    xSemaphoreGive(stateMutex);
                }
                
                const uint8_t sendLimit = (!accOn && !forceSend) ? 1 : batchSize;
                uint8_t toSend = count > sendLimit ? sendLimit : count;
                auto records = backlog.peekRecords(toSend);
                if (records.size() != toSend) {
                    Serial.println("Backlog read failed; preserving queue and reconnecting.");
                    addWebLog("ERR: Backlog read failed");
                    is_ready_to_send = false;
                    init_step = 6;
                    action_timer = millis();
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                auto packet = Codec8E::buildAVLDataPacket(records);
                const uint32_t dataFieldLength = (static_cast<uint32_t>(packet[4]) << 24) |
                                                 (static_cast<uint32_t>(packet[5]) << 16) |
                                                 (static_cast<uint32_t>(packet[6]) << 8) | packet[7];
                const uint32_t packetCrc = (static_cast<uint32_t>(packet[packet.size() - 4]) << 24) |
                                           (static_cast<uint32_t>(packet[packet.size() - 3]) << 16) |
                                           (static_cast<uint32_t>(packet[packet.size() - 2]) << 8) |
                                           packet[packet.size() - 1];
                Serial.printf("Codec8E batch: records=%u/%u, dataLength=%lu, packetBytes=%u, "
                              "crc=%04lX, reason=%s\n",
                              packet[9], packet[8 + dataFieldLength - 1], dataFieldLength,
                              static_cast<unsigned>(packet.size()), packetCrc,
                              forceSend ? "forced" : (batchFull ? "full" : "timeout"));
                for (uint8_t i = 0; i < toSend; ++i) {
                    Serial.printf("  AVL[%u]: ts=%llu, lat=%.7f, lon=%.7f, sats=%u\n",
                                  i, static_cast<unsigned long long>(records[i].gps.utcTime),
                                  records[i].gps.latitude, records[i].gps.longitude,
                                  records[i].gps.satellites);
                }
                
                // ACK Retry logic: retry up to 3 times (REQUIREMENTS §4)
                bool ackOk = false;
                for (int retry = 0; retry < 3 && !ackOk; retry++) {
                    if (retry > 0) {
                        Serial.printf("ACK Retry %d/3...\n", retry + 1);
                        addWebLog(String("TX: Retry ") + (retry + 1));
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                    
                    if (ec800.sendTCP(packet)) {
                        addWebLog(String("TX: AVL Data (") + toSend + " records)");
                        std::vector<uint8_t> ack;
                        if (ec800.readTCP(ack, 5000, 4) && ack.size() == 4) {
                            uint32_t ackNum = (static_cast<uint32_t>(ack[0]) << 24) |
                                              (static_cast<uint32_t>(ack[1]) << 16) |
                                              (static_cast<uint32_t>(ack[2]) << 8) | ack[3];
                            if (ackNum == toSend) {
                                if (backlog.commitRecords(toSend)) {
                                    Serial.printf("Sent and ACKed %d records.\n", toSend);
                                    addWebLog(String("RX: ACK ") + ackNum);
                                    net_err_cnt = 0;
                                    ackOk = true;
                                    lastBatchSend = millis();
                                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                        // A forced flush continues until the persisted queue is
                                        // empty, including during the ON -> OFF transition.
                                        state.forceSendRequested = forceSend && backlog.getRecordCount() > 0;
                                        xSemaphoreGive(stateMutex);
                                    }
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                } else {
                                    Serial.println("ACK received but queue metadata commit failed; records will be replayed.");
                                    addWebLog("ERR: Backlog commit failed; replaying");
                                }
                            } else {
                                Serial.printf("ACK mismatch: expected %d, got %lu\n", toSend, ackNum);
                            }
                        } else {
                            Serial.println("ACK timeout or short");
                        }
                    } else {
                        // TCP Send failed — no point retrying
                        break;
                    }
                }
                
                if (!ackOk) {
                    // FIX-CRIT-02: At-least-once delivery. Never discard a
                    // record without the exact server ACK; reconnect and replay.
                    is_ready_to_send = false;
                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        state.tcpConnected = false;
                        xSemaphoreGive(stateMutex);
                    }
                    ec800.closeTCP();
                    init_step = 6;
                    action_timer = millis();
                }
            } else {
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    state.uploadingBacklog = false;
                    xSemaphoreGive(stateMutex);
                }
                
                // Read Command from Server
                std::vector<uint8_t> cmdPacket;
                if (ec800.readTCP(cmdPacket, 100)) {
                    String hexStr = "";
                    Serial.print("Raw Command Packet: ");
                    for (uint8_t b : cmdPacket) {
                        if (b < 0x10) { Serial.print("0"); hexStr += "0"; }
                        Serial.print(b, HEX);
                        hexStr += String(b, HEX) + " ";
                        Serial.print(" ");
                    }
                    Serial.println();
                    hexStr.toUpperCase();
                    addWebLog("RX Raw: " + hexStr);

                    String command;
                    if (Codec8E::parseCommand(cmdPacket, command)) {
                        Serial.println("Received Command: " + command);
                        addWebLog("RX Command: " + command);
                        
                        // --- Full Command Handler (REQUIREMENTS §5) ---
                        String response = "OK";
                        String commandKey = command;
                        commandKey.toLowerCase();
                        
                        if (commandKey == "acc1" || commandKey == "acc0") {
                            const bool enabled = commandKey.endsWith("1");
                            bool updated = false;
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                // A remote ACC command explicitly selects virtual control.
                                // Task_Sensors and the web dashboard then consume the same
                                // state, so neither side can overwrite the other unexpectedly.
                                state.usePhysicalAcc = false;
                                state.virtualAccState = enabled;
                                state.accState = enabled;
                                updated = true;
                                xSemaphoreGive(stateMutex);
                            }
                            if (updated) {
                                configManager.save();
                                response = String("ACC=") + (enabled ? "1" : "0") + ",MODE=VIRTUAL";
                                Serial.println("Remote virtual ACC updated: " + response);
                                addWebLog("CMD: " + response);
                            } else {
                                response = "ERR: state busy";
                            }

                        } else if (commandKey == "acc?") {
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                response = String("ACC=") + (state.accState ? "ON" : "OFF") +
                                        ",MODE=" + (state.usePhysicalAcc ? "PHYSICAL" : "VIRTUAL") +
                                        ",IO239=" + (state.accState ? "1" : "0") +
                                        ",DIN1=" + (state.physicalAccState ? "1" : "0") +
                                        ",SEND_MODE=" + (state.accState ? "NORMAL" : "POWER_SAVE") +
                                        ",SAMPLE=" + String(state.sampleIntervalSec) +
                                        ",BATCH=" + String(state.batchSize) +
                                        ",BACKLOG=" + String(backlog.getRecordCount());
                                xSemaphoreGive(stateMutex);
                            } else response = "ERR: state busy";

                        } else if (commandKey == "acc_mode,physical" || commandKey == "acc_mode,virtual") {
                            const bool physical = commandKey.endsWith("physical");
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                state.usePhysicalAcc = physical;
                                state.accState = physical ? state.physicalAccState : state.virtualAccState;
                                xSemaphoreGive(stateMutex);
                                configManager.save();
                                response = String("OK: ACC mode set to ") + (physical ? "physical" : "virtual");
                            } else response = "ERR: state busy";

                        } else if (commandKey.startsWith("acc_mode,")) {
                            response = "ERR: use acc_mode,physical or acc_mode,virtual";

                        } else if (commandKey == "force_send") {
                            if (backlog.getRecordCount() == 0) {
                                SystemState heartbeat;
                                bool canQueue = false;
                                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    heartbeat = state;
                                    canQueue = state.hasLastKnownLocation;
                                    xSemaphoreGive(stateMutex);
                                }
                                const time_t currentTime = time(nullptr);
                                if (canQueue && currentTime > 1000000000) {
                                    heartbeat.gps.utcTime = static_cast<uint64_t>(currentTime) * 1000ULL;
                                    heartbeat.gps.isValid = false;
                                    heartbeat.gps.satellites = 0;
                                    heartbeat.gps.speed = 0;
                                    heartbeat.gps.course = 0;
                                    heartbeat.gps.altitude = 0;
                                    heartbeat.gps.pdop = 0;
                                    heartbeat.gps.hdop = 0;
                                    backlog.pushRecord(heartbeat);
                                }
                            }
                            const uint32_t pending = backlog.getRecordCount();
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                state.forceSendRequested = pending > 0;
                                xSemaphoreGive(stateMutex);
                            }
                            response = pending > 0 ? String("OK: force_send queued, records=") + pending
                                                   : "ERR: no backlog or last known location";

                        } else if (commandKey == "reconnect") {
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                state.reconnectRequested = true;
                                state.forceSendRequested = backlog.getRecordCount() > 0;
                                xSemaphoreGive(stateMutex);
                            }
                            response = "OK: reconnect requested; backlog preserved";

                        } else if (commandKey == "gnss_restart") {
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                state.gnssRestartRequested = true;
                                xSemaphoreGive(stateMutex);
                            }
                            response = "OK: GNSS restart requested";

                        } else if (commandKey == "backlog") {
                            const uint32_t pending = backlog.getRecordCount();
                            uint64_t oldest = 0, newest = 0;
                            if (backlog.getTimeRange(oldest, newest)) {
                                response = String("BACKLOG=") + pending + ",OLDEST=" + formatUtcTimestamp(oldest) +
                                           ",NEWEST=" + formatUtcTimestamp(newest);
                            } else response = "ERR: backlog read failed";

                        } else if (commandKey.startsWith("set_batch,")) {
                            const int value = commandKey.substring(10).toInt();
                            if (value >= 1 && value <= 20) {
                                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    state.batchSize = value;
                                    xSemaphoreGive(stateMutex);
                                }
                                configManager.save();
                                response = String("OK: batch size set to ") + value;
                            } else response = "ERR: batch size must be 1-20";

                        } else if (commandKey.startsWith("set_sample,")) {
                            const int value = commandKey.substring(11).toInt();
                            if (value >= 5 && value <= 300) {
                                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    state.sampleIntervalSec = value;
                                    xSemaphoreGive(stateMutex);
                                }
                                configManager.save();
                                response = String("OK: GPS sample interval set to ") + value + " seconds";
                            } else response = "ERR: sample interval must be 5-300 seconds";

                        } else if (commandKey == "reboot" || commandKey == "restart" ||
                                   commandKey == "device_restart") {
                            configManager.save();
                            response = String("OK: restarting device; backlog saved=") + backlog.getRecordCount();
                            auto respPacket = Codec8E::buildCommandResponse(1, response);
                            ec800.sendTCP(respPacket);
                            vTaskDelay(pdMS_TO_TICKS(500));
                            ESP.restart();
                             
                        } else if (commandKey == "config" || commandKey == "status") {
                            // Return device info as text
                            String info = "FW=" FW_VERSION;
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                info += ", IMEI=" + state.imei;
                                info += ", VBAT=" + String(state.vbatVoltage, 2);
                                info += ", RSSI=" + String(state.rssi);
                                info += ", ACC=" + String(state.accState ? 1 : 0);
                                info += ", ACC_MODE=" + String(state.usePhysicalAcc ? "PHYSICAL" : "VIRTUAL");
                                info += ", GPS=" + String(state.gps.isValid ? "FIX" : "NOFIX");
                                info += ", SAT=" + String(state.gps.satellites);
                                info += ", LAST_LOC=" + formatUtcTimestamp(state.gps.utcTime);
                                info += ", INTERVAL=" + String(state.intervalSec);
                                info += ", SAMPLE=" + String(state.sampleIntervalSec);
                                info += ", BATCH=" + String(state.batchSize);
                                info += ", SEND_MODE=" + String(state.accState ? "NORMAL" : "POWER_SAVE");
                                info += ", OVERSPEED=" + String(state.overspeedLimit);
                                info += ", MILEAGE_M=" + String(state.totalMileage);
                                info += ", BACKLOG=" + String(backlog.getRecordCount());
                                info += ", HEAP=" + String(state.freeHeap);
                                xSemaphoreGive(stateMutex);
                            }
                            response = info;
                            // Also enable WiFi if it was off
                            if (!wifiEnabled) {
                                WiFi.mode(WIFI_AP);
                                WiFi.softAP(state.apSSID.c_str(), state.apPass.c_str());
                                wifiEnabled = true;
                                lastWifiClientTime = millis();
                                addWebLog("WiFi AP Enabled via command");
                            }
                            
                        } else if (commandKey == "beep") {
                            indicators.beep(3, 150);
                            response = "Beeping";
                            
                        } else if (commandKey == "led,on") {
                            indicators.setLedForce(1);
                            response = "LED ON";
                            
                        } else if (commandKey == "led,off") {
                            indicators.setLedForce(-1);
                            response = "LED OFF";
                            
                        } else if (commandKey == "gps") {
                            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                response = String(state.gps.latitude, 6) + "," + String(state.gps.longitude, 6);
                                response += ", SPD=" + String(state.gps.speed, 1);
                                response += ", SAT=" + String(state.gps.satellites);
                                response += ", FIX=" + String(state.gps.isValid ? "Y" : "N");
                                xSemaphoreGive(stateMutex);
                            }
                            
                        } else if (commandKey == "fingerprint") {
                            // FIX-CRIT-06: Network task must never touch UART2;
                            // the Sensors task is the single fingerprint owner.
                            response = "FP_COUNT=" + String(fingerprint.getCachedUserCount());
                            
                        } else if (commandKey == "factory_reset") {
                            factoryResetCode = 100000 + esp_random() % 900000;
                            factoryResetCodeExpires = millis() + 60000;
                            response = String("Factory reset requires confirmation. Send: factory_reset,") + factoryResetCode;

                        } else if (commandKey.startsWith("factory_reset,")) {
                            const uint32_t suppliedCode = commandKey.substring(14).toInt();
                            if (factoryResetCode != 0 && suppliedCode == factoryResetCode &&
                                static_cast<int32_t>(factoryResetCodeExpires - millis()) >= 0) {
                                response = "OK: factory reset completed; backlog preserved; restarting";
                                auto respPacket = Codec8E::buildCommandResponse(1, response);
                                ec800.sendTCP(respPacket);
                                configManager.factoryReset();
                                factoryResetCode = 0;
                                vTaskDelay(pdMS_TO_TICKS(500));
                                ESP.restart();
                            } else {
                                response = "ERR: invalid or expired factory reset confirmation";
                            }
                            auto respPacket = Codec8E::buildCommandResponse(1, response);
                            ec800.sendTCP(respPacket);
                            backlog.clearAll();
                            configManager.factoryReset();
                            vTaskDelay(pdMS_TO_TICKS(500));
                            ESP.restart();
                            
                        } else if (commandKey.startsWith("set_interval,")) {
                            int newInterval = commandKey.substring(13).toInt();
                            if (newInterval >= 5 && newInterval <= 3600) {
                                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    state.intervalSec = newInterval;
                                    xSemaphoreGive(stateMutex);
                                }
                                configManager.save();
                                response = "INTERVAL=" + String(newInterval);
                            } else {
                                response = "ERR: interval must be 5-3600";
                            }
                            
                        } else if (commandKey.startsWith("set_server,")) {
                            // Format: set_server,host,port
                            String params = command.substring(11);
                            int commaIdx = params.indexOf(',');
                            if (commaIdx > 0) {
                                String newHost = params.substring(0, commaIdx);
                                int newPort = params.substring(commaIdx + 1).toInt();
                                if (isSafeHost(newHost) && newPort > 0 && newPort < 65536) {
                                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                        state.serverHost = newHost;
                                        state.serverPort = newPort;
                                        xSemaphoreGive(stateMutex);
                                    }
                                    configManager.save();
                                    response = "SERVER=" + newHost + ":" + String(newPort);
                                } else {
                                    response = "ERR: invalid host/port";
                                }
                            } else {
                                response = "ERR: format set_server,host,port";
                            }
                        } else {
                            response = "ERR: unknown command";
                        }
                        
                        auto respPacket = Codec8E::buildCommandResponse(1, response);
                        ec800.sendTCP(respPacket);
                        addWebLog("TX: Resp=" + response);
                    }
                }
            }
            
            // Periodically check if network is still alive
            static unsigned long lastNetCheck = 0;
            if (millis() - lastNetCheck > 10000) {
                if (!ec800.isNetworkRegistered()) {
                    is_ready_to_send = false;
                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        state.tcpConnected = false;
                        state.networkConnected = false;
                        xSemaphoreGive(stateMutex);
                    }
                    init_step = 5; // Go back to network check
                    action_timer = millis();
                }
                lastNetCheck = millis();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Sensors Task
void Task_Sensors(void *pvParameters) {
    uint32_t lastRecordSave = 0;
    uint32_t lastHeapUpdate = 0;
    uint32_t lastStatusHeartbeat = millis();
    uint64_t lastSavedUtcTime = 0;
    bool previousAccState = false;
    bool accStateInitialized = false;
    
    while (true) {
        readVBAT();
        
        bool accOn = false;
        uint16_t sampleIntervalSec = DEFAULT_GPS_SAMPLE_INTERVAL_SEC;
        uint8_t batchSize = DEFAULT_BATCH_SIZE;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            state.physicalAccState = digitalRead(ACC_IGNITION_PIN) == HIGH;
            state.accState = state.usePhysicalAcc ? state.physicalAccState : state.virtualAccState;
            accOn = state.accState;
            sampleIntervalSec = state.sampleIntervalSec;
            batchSize = state.batchSize;
            xSemaphoreGive(stateMutex);
        }

        if (!accStateInitialized) {
            previousAccState = accOn;
            accStateInitialized = true;
            Serial.printf("Initial send mode: %s\n", accOn ? "normal" : "power_save");
            addWebLog(accOn ? "Send mode: normal" : "Send mode: power_save (300s)");
        } else if (accOn != previousAccState) {
            Serial.printf("ACC changed: %s -> %s\n", previousAccState ? "ON" : "OFF", accOn ? "ON" : "OFF");
            addWebLog(accOn ? "Leaving power_save mode" : "Entering power_save mode (300s)");
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                // Flush pending driving records before settling into power save,
                // and flush persisted records immediately when driving resumes.
                state.forceSendRequested = backlog.getRecordCount() > 0;
                xSemaphoreGive(stateMutex);
            }
            lastStatusHeartbeat = millis();
            previousAccState = accOn;
        }
        
        if (millis() - lastHeapUpdate >= 1000) {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                state.freeHeap = ESP.getFreeHeap();
                state.temperature = temperatureRead(); // ESP32 internal temp sensor
                xSemaphoreGive(stateMutex);
            }
            lastHeapUpdate = millis();
        }
        
        // Sample independently from the upload interval. Each snapshot keeps
        // its own GNSS timestamp and is later encoded as a separate AVL record.
        if (accOn && millis() - lastRecordSave >= sampleIntervalSec * 1000UL) {
            SystemState snapshot;
            bool captured = false;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                snapshot.gps = state.gps;
                snapshot.vbatVoltage = state.vbatVoltage;
                snapshot.vbatPercent = state.vbatPercent;
                snapshot.accState = state.accState;
                snapshot.totalMileage = state.totalMileage;
                snapshot.rssi = state.rssi;
                snapshot.temperature = state.temperature;
                captured = true;
                xSemaphoreGive(stateMutex);
            }
            // FIX-CRIT-06: Flash I/O must not happen while stateMutex is held.
            if (captured) {
                if (!snapshot.gps.isValid || snapshot.gps.satellites == 0) {
                    // Invalid fixes never enter the high-rate GPS position buffer.
                } else if (snapshot.gps.utcTime < 1000000000000ULL) {
                    addWebLog("WARN: Record skipped until UTC is known");
                } else if (snapshot.gps.utcTime <= lastSavedUtcTime) {
                    addWebLog("WARN: GPS sample skipped (duplicate timestamp)");
                } else if (!backlog.pushRecord(snapshot)) {
                    addWebLog("ERR: Backlog full/write failed");
                } else {
                    lastSavedUtcTime = snapshot.gps.utcTime;
                    lastStatusHeartbeat = millis();
                    Serial.printf("GPS sample queued: ts=%llu, backlog=%lu/%u\n",
                                  static_cast<unsigned long long>(snapshot.gps.utcTime),
                                  backlog.getRecordCount(), batchSize);
                }
                lastRecordSave = millis();
            }
        }

        // Codec8E AVL records always contain a GPS element. When the live fix
        // is lost, reuse only a genuinely known location and mark every GNSS
        // field invalid/zero. If no location has ever been known, keep the TCP
        // session alive but do not encode a fake (0, 0) position.
        uint32_t heartbeatIntervalSec = accOn ? DEFAULT_INTERVAL_SEC : POWER_SAVE_INTERVAL_SEC;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            heartbeatIntervalSec = accOn ? state.intervalSec : POWER_SAVE_INTERVAL_SEC;
            xSemaphoreGive(stateMutex);
        }
        if (millis() - lastStatusHeartbeat >=
                static_cast<uint64_t>(heartbeatIntervalSec) * 1000ULL) {
            SystemState heartbeat;
            bool captured = false;
            bool hasLastKnownLocation = false;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                heartbeat.gps = state.gps;
                heartbeat.vbatVoltage = state.vbatVoltage;
                heartbeat.vbatPercent = state.vbatPercent;
                heartbeat.accState = state.accState;
                heartbeat.totalMileage = state.totalMileage;
                heartbeat.rssi = state.rssi;
                heartbeat.temperature = state.temperature;
                hasLastKnownLocation = state.hasLastKnownLocation;
                captured = !state.gps.isValid || !accOn;
                xSemaphoreGive(stateMutex);
            }

            if (captured && backlog.getRecordCount() == 0) {
                const time_t now = time(nullptr);
                const bool hasHeartbeatTime = heartbeat.gps.utcTime >= 1000000000000ULL || now > 1000000000;
                if (hasLastKnownLocation && hasHeartbeatTime) {
                    if (!accOn && now > 1000000000) {
                        heartbeat.gps.utcTime = static_cast<uint64_t>(now) * 1000ULL;
                    }
                    heartbeat.gps.isValid = false;
                    heartbeat.gps.satellites = 0;
                    heartbeat.gps.speed = 0.0f;
                    heartbeat.gps.course = 0.0f;
                    heartbeat.gps.altitude = 0.0f;
                    heartbeat.gps.pdop = 0.0f;
                    heartbeat.gps.hdop = 0.0f;
                    if (backlog.pushRecord(heartbeat)) {
                        Serial.printf("Status heartbeat queued: ts=%llu, lastKnown=%.7f,%.7f, GNSS=0\n",
                                      static_cast<unsigned long long>(heartbeat.gps.utcTime),
                                      heartbeat.gps.latitude, heartbeat.gps.longitude);
                        addWebLog("TX queue: status heartbeat (GNSS no fix)");
                    } else {
                        addWebLog("ERR: Status heartbeat queue failed");
                    }
                } else {
                    Serial.println("No GPS fix and no last known location, "
                                   "sending status-only heartbeat if supported: "
                                   "Codec8E AVL requires a GPS element, keeping TCP session alive instead");
                    addWebLog("NOFIX: No last location; TCP heartbeat only");
                }
            }
            lastStatusHeartbeat = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Fingerprint operations can wait for multiple scans. Isolating them prevents
// a long enrollment from delaying ACC/VBAT sampling and backlog scheduling.
void Task_Fingerprint(void *pvParameters) {
    while (true) {
        fingerprint.loop();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Indicators Task
void Task_Indicators(void *pvParameters) {
    // Reuse the snapshot to avoid constructing/destructing Arduino Strings at 20 Hz.
    SystemState copyState;
    while (true) {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            copyState.uploadingBacklog = state.uploadingBacklog;
            copyState.networkConnected = state.networkConnected;
            copyState.tcpConnected = state.tcpConnected;
            copyState.gps.isValid = state.gps.isValid;
            xSemaphoreGive(stateMutex);
        }
        indicators.update(copyState);
        vTaskDelay(pdMS_TO_TICKS(50)); // Fast update rate for smooth audio and LED blinking
    }
}

void setup() {
    vTaskDelay(pdMS_TO_TICKS(3000)); // Wait for USB CDC to enumerate
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Prevents ESP32-S3 from crashing when USB is unplugged
    Serial.println("\n\n--- Starting ESP32-S3 GPS Tracker V2 ---");
    Serial.printf("Firmware tag: v%s (Arduino-ESP32 %s)\n",
                  FW_VERSION, ESP_ARDUINO_VERSION_STR);
    const uint32_t psramSize = ESP.getPsramSize();
    Serial.printf("PSRAM: %s, size=%u bytes\n", psramSize > 0 ? "USABLE" : "UNAVAILABLE", psramSize);
    Serial.printf("Flash physical size: %u bytes\n", ESP.getFlashChipSize());
    if (psramSize == 0) {
        Serial.println("WARNING: PSRAM flag is present but no SPIRAM heap is usable; check early boot log/module type.");
    }
    
    stateMutex = xSemaphoreCreateMutex();
    webLogsMutex = xSemaphoreCreateMutex();
    state.freeHeap = ESP.getFreeHeap(); // Valid value for the first immediate alive log.
    
    // Load config from NVS
    configManager.begin();
    
    // Start Web Dashboard
    dashboard.begin();
    
    pinMode(ACC_IGNITION_PIN, INPUT_PULLDOWN);
    pinMode(VBAT_ADC_PIN, INPUT);
    
    indicators.begin();
    // REQUIREMENTS: two short beeps at boot. The former tune conflicted with
    // the independent non-blocking beep state machine.
    indicators.beep(2, 120);
    
    if (!backlog.begin()) {
        Serial.println("Failed to mount backlog filesystem!");
    } else {
        Serial.printf("LittleFS Mounted. Backlog records: %d\n", backlog.getRecordCount());
    }
    
    if (fingerprint.begin(57600, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN)) {
        fingerprint.setCallbacks(onFingerprintMatch, onFingerprintNoMatch);
        Serial.println("Fingerprint Sensor Initialized (Custom Protocol).");
    } else {
        Serial.println("Fingerprint Sensor Init Failed!");
    }
    
    xTaskCreatePinnedToCore(Task_Network, "Network", 8192, NULL, 1, &NetworkTaskHandle, 1);
    xTaskCreatePinnedToCore(Task_Sensors, "Sensors", 4096, NULL, 1, &SensorsTaskHandle, 0);
    xTaskCreatePinnedToCore(Task_Fingerprint, "Fingerprint", 4096, NULL, 1, &FingerprintTaskHandle, 0);
    xTaskCreatePinnedToCore(Task_Indicators, "Indicators", 3072, NULL, 1, &IndicatorsTaskHandle, 0);
    
    lastWifiClientTime = millis();
}

unsigned long lastAliveLog = 0;

void loop() {
    // Handle Web Server Clients
    if (wifiEnabled) {
        dashboard.handleClient();
        
        if (WiFi.softAPgetStationNum() > 0) {
            lastWifiClientTime = millis();
        } else if (millis() - lastWifiClientTime > 300000) {
            Serial.println("No WiFi clients for 5 minutes. Turning off WiFi AP.");
            addWebLog("WiFi Auto-Off");
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_OFF);
            wifiEnabled = false;
        }
    }
    
    if (millis() - lastAliveLog > 5000) {
        uint32_t freeHeap = 0;
        bool net = false, tcp = false, gpsValid = false;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            freeHeap = state.freeHeap;
            net = state.networkConnected;
            tcp = state.tcpConnected;
            gpsValid = state.gps.isValid;
            xSemaphoreGive(stateMutex);
        }
        Serial.printf("[SYSTEM] Alive. Heap: %u, Network: %d, TCP: %d, GPS Fix: %d\n", 
                      freeHeap, net, tcp, gpsValid);
        lastAliveLog = millis();
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
}
