#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Hardware Pins ---
#define EC800_PWRKEY 38
#define EC800_RST    39
#define EC800_TX     4
#define EC800_RX     5  
#define WS2812_PIN   47
#define LED1_PIN     21
#define BUZZER_PIN   41

#define NUMPIXELS 1

// --- Network & JT808 Settings ---
#define APN "v-internet"
#define SERVER_IP "your-server.com"
#define SERVER_PORT 5015

#define DEFAULT_LAT 21.03678847235882
#define DEFAULT_LON 105.83467128147198

// --- Device State ---
enum DeviceState {
  STATE_INIT = 0,
  STATE_STARTUP_SOUND,
  STATE_NO_SIM,
  STATE_NO_NETWORK,
  STATE_NO_INTERNET,
  STATE_NO_GPS,
  STATE_HAS_GPS
};

extern volatile DeviceState currentState;
extern String terminal_id;
extern String modem_imei;
extern String modem_ccid;
extern double current_lat;
extern double current_lon;
extern String current_time;

#endif
