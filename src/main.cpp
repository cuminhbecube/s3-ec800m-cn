#include <Arduino.h>
#include "Config.h"
#include "Indicator.h"
#include "ModemJT808.h"
#include "WebDashboard.h"

// Initialize globals defined in Config.h
volatile DeviceState currentState = STATE_INIT;
String terminal_id = "012345678912";

void webTaskFunc(void *pvParameters) {
  for (;;) {
    web_loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  Serial.println();
  Serial.println("-I-Flash OK: EF4018");
  Serial.printf("-Os heap free:%d\n", ESP.getFreeHeap());

  indicator_init();
  web_init();
  
  currentState = STATE_STARTUP_SOUND;

  modem_init();
  
  // Tạo task riêng cho WebServer để không bị block bởi modem_loop
  xTaskCreatePinnedToCore(
    webTaskFunc,
    "WebTask",
    8192,
    NULL,
    1,
    NULL,
    1
  );
}

void loop() {
  modem_loop();
}