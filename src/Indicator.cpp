#include "Indicator.h"
#include "Config.h"
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel pixels(NUMPIXELS, WS2812_PIN, NEO_GRB + NEO_KHZ800);

void indicatorTask(void *pvParameters) {
  bool ledToggle = false;
  unsigned long lastLedBlink = 0;
  
  // Buzzer timing
  unsigned long lastBuzzer = 0;
  int buzzerPhase = 0;

  for (;;) {
    unsigned long now = millis();
    
    // LED blink timer
    if (now - lastLedBlink > 500) {
      lastLedBlink = now;
      ledToggle = !ledToggle;
    }

    uint32_t color = 0;
    bool shouldBlink = false;

    // Buzzer params
    int buzzerFreq = 0;
    int buzzerDur = 0;
    bool triggerBuzzer = false;

    switch (currentState) {
      case STATE_STARTUP_SOUND:
        color = pixels.Color(50, 0, 0); // Đỏ lúc khởi động
        shouldBlink = false;
        if (buzzerPhase == 0) {
          tone(BUZZER_PIN, 1000, 100);
          lastBuzzer = now;
          buzzerPhase = 1;
        } else if (buzzerPhase == 1 && now - lastBuzzer > 150) {
          tone(BUZZER_PIN, 1500, 100);
          lastBuzzer = now;
          buzzerPhase = 2;
        } else if (buzzerPhase == 2 && now - lastBuzzer > 150) {
          tone(BUZZER_PIN, 2000, 200);
          lastBuzzer = now;
          buzzerPhase = 3;
        } else if (buzzerPhase == 3 && now - lastBuzzer > 250) {
          currentState = STATE_INIT; // Kết thúc nhạc
          buzzerPhase = 0;
        }
        break;
      case STATE_NO_SIM:
        color = pixels.Color(50, 0, 50); // Nháy tím
        shouldBlink = true;
        // Bíp liên tục (mỗi 300ms)
        if (now - lastBuzzer > 300) {
          lastBuzzer = now;
          triggerBuzzer = true;
          buzzerFreq = 2000;
          buzzerDur = 100;
        }
        break;
      case STATE_NO_NETWORK:
        color = pixels.Color(50, 0, 0); // Đỏ đứng
        shouldBlink = false;
        // 1 tiếng dài mỗi 3s
        if (now - lastBuzzer > 3000) {
          lastBuzzer = now;
          triggerBuzzer = true;
          buzzerFreq = 1000;
          buzzerDur = 500;
        }
        break;
      case STATE_NO_INTERNET:
        color = pixels.Color(50, 50, 0); // Nháy vàng
        shouldBlink = true;
        // 2 tiếng ngắn mỗi 3s
        if (now - lastBuzzer > 3000) {
          lastBuzzer = now;
          buzzerPhase = 1;
        }
        if (buzzerPhase == 1) {
          tone(BUZZER_PIN, 1500, 100);
          buzzerPhase = 2;
          lastBuzzer = now; // reset timer for gap
        } else if (buzzerPhase == 2 && now - lastBuzzer > 200) {
          tone(BUZZER_PIN, 1500, 100);
          buzzerPhase = 0;
        }
        break;
      case STATE_NO_GPS:
        color = pixels.Color(0, 0, 50); // Blue đứng
        shouldBlink = false;
        // Đã tắt còi báo lỗi GPS theo yêu cầu
        /*
        if (now - lastBuzzer > 5000) {
          lastBuzzer = now;
          triggerBuzzer = true;
          buzzerFreq = 2500;
          buzzerDur = 50;
        }
        */
        break;
      case STATE_HAS_GPS:
        color = pixels.Color(0, 50, 0); // Green đứng
        shouldBlink = false;
        // Không kêu
        buzzerPhase = 0;
        break;
      case STATE_INIT:
      default:
        color = pixels.Color(50, 0, 0); // Đỏ lúc khởi tạo mạng
        shouldBlink = false;
        break;
    }

    if (shouldBlink && !ledToggle) {
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    } else {
      pixels.setPixelColor(0, color);
    }
    pixels.show();
    
    if (triggerBuzzer && buzzerFreq > 0) {
      tone(BUZZER_PIN, buzzerFreq, buzzerDur);
    }
    
    vTaskDelay(50 / portTICK_PERIOD_MS); // Update quickly for accurate tone timing
  }
}

void indicator_init() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED1_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(50, 0, 0));
  pixels.show();

  xTaskCreatePinnedToCore(indicatorTask, "Indicator_Task", 2048, NULL, 1, NULL, 1);
}
