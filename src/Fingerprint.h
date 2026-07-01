#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <Arduino.h>

// ── Basic response codes ──
#define ACK_SUCCESS         0x00
#define ACK_FAIL            0x01
#define ACK_FULL            0x04
#define ACK_NO_USER         0x05
#define ACK_USER_OCCUPIED   0x06
#define ACK_FINGER_OCCUPIED 0x07
#define ACK_TIMEOUT         0x08

#define ACK_ALL_USER        0x00
#define ACK_GUEST_USER      0x01
#define ACK_NORMAL_USER     0x02
#define ACK_MASTER_USER     0x03

// ── Command bytes ──
#define CMD_HEAD            0xF5
#define CMD_TAIL            0xF5
#define CMD_ADD_1           0x01
#define CMD_ADD_2           0x02
#define CMD_ADD_3           0x03
#define CMD_DEL             0x04
#define CMD_DEL_ALL         0x05
#define CMD_USER_CNT        0x09
#define CMD_GET_PRIVILEGE   0x0A
#define CMD_MATCH_1TO1      0x0B
#define CMD_MATCH           0x0C
#define CMD_COM_LEV         0x28
#define CMD_LP_MODE         0x2C
#define CMD_ADD_MODE        0x2D
#define CMD_TIMEOUT         0x2E

enum FingerprintState {
    FP_IDLE,
    FP_ADDING,
    FP_VERIFYING,
    FP_DELETING,
    FP_CLEARING
};

class FingerprintManager {
public:
    FingerprintManager(HardwareSerial& serial, int wakePin);
    
    bool begin(uint32_t baudrate, int rxPin, int txPin);
    void loop();
    
    void setCallbacks(void (*onMatch)(int), void (*onNoMatch)());
    
    // Internal functions exposed
    bool waitReady(uint16_t timeoutMs);
    uint16_t getUserCount(bool silent = false);
    uint8_t setCompareLevel(uint8_t level);
    uint8_t addUser(uint16_t userId, uint8_t privilege);
    uint8_t verifyUser();

    bool startAdding();
    bool startVerifying();
    bool startDeleting(uint16_t userId);
    bool startClearAll();
    bool isReady() const { return _sensorReady; }
    uint16_t getCachedUserCount();
    FingerprintState getState();
    String getLastResult();
    String getLastResultType();

private:
    HardwareSerial& _serial;
    int _wakePin;
    
    FingerprintState _state = FP_IDLE;
    int _retryCount = 0;
    uint16_t _fingerCount = 0;
    uint16_t _lastUserId = 0;
    uint16_t _pendingUserId = 0;
    volatile bool _sensorReady = false;
    portMUX_TYPE _stateMux = portMUX_INITIALIZER_UNLOCKED;
    char _lastResult[64] = "Sensor initializing";
    char _lastResultType[12] = "info";
    
    bool _fingerWasRemoved = true;
    unsigned long _lastTouchTime = 0;
    unsigned long _lastRefresh = 0;
    unsigned long _lastReconnectAttempt = 0;
    
    void (*_onMatch)(int) = nullptr;
    void (*_onNoMatch)() = nullptr;
    
    uint8_t _txBuf[8];
    uint8_t _rxBuf[8];
    
    uint8_t sendCmd8(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3, uint16_t timeoutMs, bool silent = false);
    uint8_t deleteUserCommand(uint16_t userId);
    uint8_t clearAllCommand();
    void setState(FingerprintState state);
    void setResult(const char* message, const char* type);
    void processOperation();
};

#endif // FINGERPRINT_H
