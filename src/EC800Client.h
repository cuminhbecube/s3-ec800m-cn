#ifndef EC800CLIENT_H
#define EC800CLIENT_H

#include <Arduino.h>

class EC800Client {
public:
    EC800Client(HardwareSerial& serial);
    
    void begin(int baud, int rx_pin, int tx_pin);
    
    // Core AT Command functions
    void sendAT(String cmd);
    int waitResponseStr(uint32_t timeout_ms, String& response, String expected1 = "OK", String expected2 = "ERROR");
    int waitResponse(uint32_t timeout_ms, String expected1 = "OK", String expected2 = "ERROR");
    String sendCommandWithResponse(String cmd, uint32_t timeout_ms = 1000);
    
    // Basic functions
    bool initModem();
    bool isSimReady();
    String getIMEI();
    String getCCID();
    
    // Network
    bool isNetworkConnected();
    bool configGPRS(String apn, String user, String pass);
    bool activateGPRS();
    bool isGPRSConnected();
    bool syncNTP(String server);
    bool getNetworkTime(int &year, int &month, int &day, int &hour, int &minute, int &second, int &tz_quarters);
    bool getGNSSTime(int &year, int &month, int &day, int &hour, int &minute, int &second);
    
    // TCP Client
    bool connectTCP(String host, int port);
    bool isTCPConnected();
    int sendTCPData(const uint8_t* data, size_t len);
    int availableTCP();
    int readTCPData(uint8_t* buffer, size_t max_len);
    void closeTCP();

    // GPS
    bool powerOnGPS();
    bool powerOffGPS();
    bool getGPSLocation(double& lat, double& lon);
    
    // Stream exposing for reading raw responses or URCs if needed
    HardwareSerial& stream;

private:
    void clearBuffer();
    bool tcp_connected = false;
};

#endif
