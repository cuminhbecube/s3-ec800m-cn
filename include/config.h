#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- PIN DEFINITIONS ---

// EC800 4G/GPS Module (UART1)
#define EC800_TX_PIN      4
#define EC800_RX_PIN      5
#define EC800_PWRKEY_PIN  38
#define EC800_RST_PIN     39

// Fingerprint Sensor (UART2)
#define FINGERPRINT_TX_PIN 9
#define FINGERPRINT_RX_PIN 10
#define FINGERPRINT_WAKE_PIN 11

// Power & Ignition
#define VBAT_ADC_PIN      17
#define ACC_IGNITION_PIN  3

// Indicators
#define LED_STATUS_PIN    21
#define NEOPIXEL_PIN      47
#define BUZZER_PIN        41

// --- SYSTEM CONSTANTS ---
#define FW_VERSION "1.2.0"

// Codec8E sampling and upload cadence. GPS positions are sampled frequently,
// then sent together so Traccar receives a detailed track without one TCP
// upload per sample. state.intervalSec remains the configurable upload timeout.
#define DEFAULT_INTERVAL_SEC 60
#define DEFAULT_GPS_SAMPLE_INTERVAL_SEC 10
#define DEFAULT_BATCH_SIZE 6
#define POWER_SAVE_INTERVAL_SEC 300
#define MAX_BACKLOG_RECORDS 5000

// Runtime defaults (single source of truth for NVS and SystemState)
#define DEFAULT_SERVER_HOST "gps-vunl.duckdns.org"
#define DEFAULT_SERVER_PORT 5027
#define DEFAULT_AP_SSID "S3_GPS_Tracker"
#define DEFAULT_AP_PASS "s3gpspassword"
#define DEFAULT_OVERSPEED_KPH 80

// Hardware Serial
#define SERIAL_EC800 Serial1
#define SERIAL_FINGERPRINT Serial2

#endif // CONFIG_H
