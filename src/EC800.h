#ifndef EC800_H
#define EC800_H

#include <Arduino.h>
#include <vector>

class EC800 {
public:
    EC800(HardwareSerial& serial, int pwrkeyPin = -1, int rstPin = -1);
    void begin(unsigned long baudrate, int rxPin, int txPin);
    
    bool init();
    bool getIMEI(String& imei);
    bool getCCID(String& ccid);
    
    // GPS Functions
    bool enableGPS();
    bool restartGPS();
    bool parseGPS(float& lat, float& lon, float& speed, float& course, float& alt,
                  uint8_t& sats, float& pdop, float& hdop, uint64_t& utcTime, bool& isValid);
    
    // Network Functions
    bool isNetworkRegistered();
    int getRSSI();
    bool getNetworkTime(int& year, int& month, int& day, int& hour, int& minute, int& second, int& tz_quarters);
    bool getGNSSTime(int& year, int& month, int& day, int& hour, int& minute, int& second);
    bool syncNTP(String server, int& year, int& month, int& day, int& hour, int& minute, int& second);
    
    // TCP Functions
    bool connectTCP(const String& host, uint16_t port);
    bool closeTCP();
    bool sendTCP(const std::vector<uint8_t>& data);
    bool readTCP(std::vector<uint8_t>& data, uint32_t timeoutMs, size_t maxBytes = 1500);
    
    // Reset/Watchdog
    void hwReset();

private:
    HardwareSerial& _serial;
    int _pwrkeyPin;
    int _rstPin;
    bool _tcpDataPending = false;
    
    String sendATCommand(const String& command, uint32_t timeout = 1000, const String& expectedResponse = "OK");
    void drainInput();
    std::vector<String> splitString(const String& str, char delimiter);
};

#endif // EC800_H
