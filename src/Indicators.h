#ifndef INDICATORS_H
#define INDICATORS_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "SystemState.h"

class Indicators {
public:
    Indicators(int neopixelPin, int buzzerPin, int ledPin);
    
    void begin();
    void update(const SystemState& state);
    
    void beep(int times, int durationMs = 100);
    void playStartupTune();
    
    // LED force control via server command
    void setLedForce(int mode); // 0=auto, 1=force on, -1=force off

private:
    int _buzzerPin;
    uint8_t _buzzerChannel = 7;
    int _ledPin;
    Adafruit_NeoPixel _pixels;
    bool _ledToggle;
    unsigned long _ledBlinkTimer = 0;
    int _ledForceMode = 0; // 0=auto, 1=force on, -1=force off
    
    // Non-blocking beep state machine
    int _beepCount = 0;
    int _beepTotal = 0;
    int _beepDuration = 0;
    unsigned long _beepTimer = 0;
    bool _beepToneOn = false;
    
    // Non-blocking startup tune state machine
    bool _tuneActive = false;
    int _tuneStep = 0;
    unsigned long _tuneTimer = 0;
    bool _tuneNoteOn = false;
    SemaphoreHandle_t _mutex = nullptr;
    
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void processBeep();
    void processTune();
};

#endif // INDICATORS_H
