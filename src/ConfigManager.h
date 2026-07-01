#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "SystemState.h"

class ConfigManager {
public:
    ConfigManager();
    void begin();
    
    void load();
    void save();
    void saveLocation(float lat, float lon);
    void saveMileage(uint32_t mileageMeters);
    void factoryReset();
    
private:
    Preferences _prefs;
    SemaphoreHandle_t _prefsMutex = nullptr;
};

#endif // CONFIG_MANAGER_H
