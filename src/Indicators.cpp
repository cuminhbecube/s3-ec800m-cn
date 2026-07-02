#include "Indicators.h"
#include "esp32-hal-ledc.h"
#include "esp_arduino_version.h"

static void buzzerWriteTone(uint8_t pin, uint8_t channel, uint32_t frequency) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    (void)channel;
    ledcWriteTone(pin, frequency);
#else
    (void)pin;
    ledcWriteTone(channel, frequency);
#endif
}

Indicators::Indicators(int neopixelPin, int buzzerPin, int ledPin) : 
    _buzzerPin(buzzerPin),
    _ledPin(ledPin), 
    _pixels(1, neopixelPin, NEO_GRB + NEO_KHZ800),
    _ledToggle(false) {
}

void Indicators::begin() {
    _mutex = xSemaphoreCreateMutex();
    pinMode(_buzzerPin, OUTPUT);
    digitalWrite(_buzzerPin, LOW);
    // Keep one LEDC channel attached for the device lifetime. Arduino core
    // noTone() detaches first and then writes the channel, which emits
    // "LEDC is not initialized" on this framework version.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(_buzzerPin, 2000, 10);
#else
    ledcSetup(_buzzerChannel, 2000, 10);
    ledcAttachPin(_buzzerPin, _buzzerChannel);
#endif
    buzzerWriteTone(_buzzerPin, _buzzerChannel, 0);
    
    pinMode(_ledPin, OUTPUT);
    digitalWrite(_ledPin, HIGH); // Default HIGH
    
    _pixels.begin();
    _pixels.clear();
    _pixels.show();
}

void Indicators::update(const SystemState& state) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    // Process non-blocking beep & startup tune
    processBeep();
    processTune();
    
    // LED blink at ~1Hz independent of task rate
    if (millis() - _ledBlinkTimer >= 500) {
        _ledToggle = !_ledToggle;
        _ledBlinkTimer = millis();
    }
    uint8_t r = 0, g = 0, b = 0;
    bool shouldBlink = false;

    // NeoPixel color priority (REQUIREMENTS):
    // 1. Uploading backlog → Yellow
    // 2. No network → Red
    // 3. Connecting server (no TCP) → Blue
    // 4. No GPS → Blue blink
    // 5. GPS Fix → Green
    if (state.uploadingBacklog) {
        // Vàng: Đang upload Backlog
        r = 50; g = 40; b = 0;
        shouldBlink = true;
    } else if (!state.networkConnected) {
        // Đỏ: Mất mạng
        r = 50; g = 0; b = 0;
        shouldBlink = false;
    } else if (!state.tcpConnected) {
        // Xanh dương: Đang kết nối Server
        r = 0; g = 0; b = 50;
        shouldBlink = true;
    } else if (!state.gps.gpsFixValid) {
        // Xanh dương nhạt nhấp nháy: Chưa có GPS
        r = 0; g = 10; b = 40;
        shouldBlink = true;
    } else {
        // Xanh lá: GPS Fix
        r = 0; g = 50; b = 0;
        shouldBlink = false;
    }

    if (shouldBlink && !_ledToggle) {
        setColor(0, 0, 0);
    } else {
        setColor(r, g, b);
    }

    // Status LED control
    if (_ledForceMode == 1) {
        digitalWrite(_ledPin, LOW); // Force ON (active low)
    } else if (_ledForceMode == -1) {
        digitalWrite(_ledPin, HIGH); // Force OFF
    } else {
        // Auto: Blink when GPS fix
        if (state.gps.gpsFixValid) {
            digitalWrite(_ledPin, _ledToggle ? LOW : HIGH);
        } else {
            digitalWrite(_ledPin, HIGH);
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

void Indicators::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _pixels.setPixelColor(0, _pixels.Color(r, g, b));
    _pixels.show();
}

void Indicators::setLedForce(int mode) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    _ledForceMode = mode;
    if (_mutex) xSemaphoreGive(_mutex);
}

// Non-blocking beep using millis() state machine
void Indicators::beep(int times, int durationMs) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    if (_beepTotal > 0) {
        if (_mutex) xSemaphoreGive(_mutex);
        return; // Already beeping
    }
    _beepTotal = times;
    _beepCount = 0;
    _beepDuration = durationMs;
    _beepToneOn = false;
    _beepTimer = millis();
    if (_mutex) xSemaphoreGive(_mutex);
}

void Indicators::processBeep() {
    if (_beepTotal == 0) return;
    
    unsigned long now = millis();
    
    if (!_beepToneOn) {
        // Start tone
        buzzerWriteTone(_buzzerPin, _buzzerChannel, 2000);
        _beepToneOn = true;
        _beepTimer = now;
    } else if (now - _beepTimer >= (unsigned long)_beepDuration) {
        // Tone finished
        buzzerWriteTone(_buzzerPin, _buzzerChannel, 0);
        _beepCount++;
        _beepToneOn = false;
        
        if (_beepCount >= _beepTotal) {
            _beepTotal = 0; // Done
        } else {
            _beepTimer = now; // Gap before next beep
        }
    }
}

// Non-blocking startup tune
void Indicators::playStartupTune() {
    _tuneActive = true;
    _tuneStep = 0;
    _tuneNoteOn = false;
    _tuneTimer = millis();
}

// Startup tune: fast ascending tech-style arpeggio (~2s total)
// C5→E5→G5→C6 (arpeggio up) → B5→G5 (resolve down) → C6 (finish)
static const int tuneNotes[]    = { 523, 659, 784, 1047, 988, 784, 1047 };
static const int tuneGaps[]     = { 150, 150, 150,  180, 130, 130,  400 };
static const int TUNE_LENGTH    = 7;

void Indicators::processTune() {
    if (!_tuneActive) return;
    
    unsigned long now = millis();
    
    if (!_tuneNoteOn) {
        // Alternate purple/cyan flash per note
        if (_tuneStep % 2 == 0) {
            setColor(30, 0, 50); // Purple
        } else {
            setColor(0, 30, 50); // Cyan
        }
        buzzerWriteTone(_buzzerPin, _buzzerChannel, tuneNotes[_tuneStep]);
        _tuneNoteOn = true;
        _tuneTimer = now;
    } else if (now - _tuneTimer >= (unsigned long)tuneGaps[_tuneStep]) {
        buzzerWriteTone(_buzzerPin, _buzzerChannel, 0);
        setColor(0, 0, 0);
        _tuneStep++;
        _tuneNoteOn = false;
        _tuneTimer = now;
        
        if (_tuneStep >= TUNE_LENGTH) {
            _tuneActive = false;
            setColor(0, 0, 0);
        }
    }
}
