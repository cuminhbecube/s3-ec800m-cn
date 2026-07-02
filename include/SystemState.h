#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <vector>
#include "config.h"

struct GPSData {
  // (0, 0) means "no known location" until hasLastKnownLocation is true.
  // It must never be encoded as an AVL position.
  float latitude = 0.0f;
  float longitude = 0.0f;
  float speed = 0.0;
  float course = 0.0;
  float altitude = 0.0;
  uint8_t satellites = 0;
  float pdop = 0.0;
  float hdop = 0.0;
  uint64_t utcTime = 0;
  // Live GNSS fix only; last-known coordinates are tracked separately below.
  bool gpsFixValid = false;
};

struct SystemState {
  // Network & Connection
  bool networkConnected = false;
  bool tcpConnected = false;
  int rssi = 0;

  // GPS
  GPSData gps;
  bool hasLastKnownLocation = false;

  // Power & IO
  float vbatVoltage = 0.0;
  uint8_t vbatPercent = 0;
  bool accState = false;
  bool physicalAccState = false;
  bool virtualAccState = false;
  bool usePhysicalAcc = true;

  // Alarm simulation/inputs exposed by the dashboard.
  bool alarmSos = false;
  bool alarmFatigue = false;
  bool alarmGpsAntenna = false;
  bool alarmPowerCut = false;
  bool alarmCollision = false;

  // Odometer
  uint32_t totalMileage = 0;
  float temperature = 0.0;

  // Configuration
  uint32_t intervalSec = DEFAULT_INTERVAL_SEC;
  uint16_t sampleIntervalSec = DEFAULT_GPS_SAMPLE_INTERVAL_SEC;
  uint8_t batchSize = DEFAULT_BATCH_SIZE;
  uint16_t overspeedLimit = DEFAULT_OVERSPEED_KPH;
  String imei = "123456789012345";
  String ccid = "Unknown";
  String serverHost = DEFAULT_SERVER_HOST;
  uint16_t serverPort = DEFAULT_SERVER_PORT;
  String apSSID = DEFAULT_AP_SSID;
  String apPass = DEFAULT_AP_PASS;

  // Status
  bool uploadingBacklog = false;
  uint32_t freeHeap = 0;

  // Requests are written by command/UI paths and consumed only by
  // Task_Network, which remains the sole owner of the modem UART.
  bool forceSendRequested = false;
  bool reconnectRequested = false;
  bool gnssRestartRequested = false;
};

extern SystemState state;
extern SemaphoreHandle_t stateMutex;
extern std::vector<String> webLogs;
extern SemaphoreHandle_t webLogsMutex;

void addWebLog(const String& message);

#endif // SYSTEM_STATE_H
