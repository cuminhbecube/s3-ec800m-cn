#include "Fingerprint.h"
#include "config.h"
#include <FreeRTOS.h>
#include <task.h>

FingerprintManager::FingerprintManager(HardwareSerial& serial, int wakePin) 
    : _serial(serial), _wakePin(wakePin) {
}

bool FingerprintManager::begin(uint32_t baudrate, int rxPin, int txPin) {
    pinMode(_wakePin, INPUT_PULLDOWN);
    _serial.begin(baudrate, SERIAL_8N1, rxPin, txPin);
    vTaskDelay(pdMS_TO_TICKS(100));
    while (_serial.available()) _serial.read();
    
    // Keep boot responsive; the dedicated task retries sensors that power up late.
    bool ready = waitReady(3000);
    _sensorReady = ready;
    if (ready) {
        uint16_t cnt = getUserCount();
        if (cnt != 0xFFFF) _fingerCount = cnt;
    }
    setResult(ready ? "Fingerprint sensor ready" : "Fingerprint sensor offline",
              ready ? "success" : "error");
    return ready;
}

void FingerprintManager::setResult(const char* message, const char* type) {
    portENTER_CRITICAL(&_stateMux);
    strlcpy(_lastResult, message, sizeof(_lastResult));
    strlcpy(_lastResultType, type, sizeof(_lastResultType));
    portEXIT_CRITICAL(&_stateMux);
}

String FingerprintManager::getLastResult() {
    char result[sizeof(_lastResult)];
    portENTER_CRITICAL(&_stateMux);
    memcpy(result, _lastResult, sizeof(result));
    portEXIT_CRITICAL(&_stateMux);
    result[sizeof(result) - 1] = '\0';
    return String(result);
}

String FingerprintManager::getLastResultType() {
    char result[sizeof(_lastResultType)];
    portENTER_CRITICAL(&_stateMux);
    memcpy(result, _lastResultType, sizeof(result));
    portEXIT_CRITICAL(&_stateMux);
    result[sizeof(result) - 1] = '\0';
    return String(result);
}

FingerprintState FingerprintManager::getState() {
    portENTER_CRITICAL(&_stateMux);
    const FingerprintState current = _state;
    portEXIT_CRITICAL(&_stateMux);
    return current;
}

void FingerprintManager::setState(FingerprintState value) {
    portENTER_CRITICAL(&_stateMux);
    _state = value;
    portEXIT_CRITICAL(&_stateMux);
}

uint16_t FingerprintManager::getCachedUserCount() {
    portENTER_CRITICAL(&_stateMux);
    const uint16_t count = _fingerCount;
    portEXIT_CRITICAL(&_stateMux);
    return count;
}

bool FingerprintManager::startAdding() {
    // FIX-CRIT-10: Web/Network only enqueue state transitions. UART2 remains
    // exclusively owned by Task_Sensors, eliminating cross-core frame races.
    portENTER_CRITICAL(&_stateMux);
    const bool accepted = _sensorReady && _state == FP_IDLE;
    if (accepted) _state = FP_ADDING;
    portEXIT_CRITICAL(&_stateMux);
    if (accepted) _retryCount = 0;
    if (accepted) setResult("Waiting for 3 enrollment scans", "info");
    return accepted;
}

bool FingerprintManager::startVerifying() {
    portENTER_CRITICAL(&_stateMux);
    const bool accepted = _sensorReady && _state == FP_IDLE;
    if (accepted) _state = FP_VERIFYING;
    portEXIT_CRITICAL(&_stateMux);
    if (accepted) _retryCount = 0;
    if (accepted) setResult("Waiting for fingerprint verification", "info");
    return accepted;
}

bool FingerprintManager::startDeleting(uint16_t userId) {
    if (userId == 0 || userId > 4095) return false;
    portENTER_CRITICAL(&_stateMux);
    const bool accepted = _sensorReady && _state == FP_IDLE;
    if (accepted) {
        _pendingUserId = userId;
        _state = FP_DELETING;
    }
    portEXIT_CRITICAL(&_stateMux);
    if (accepted) setResult("Deleting fingerprint", "warning");
    return accepted;
}

bool FingerprintManager::startClearAll() {
    portENTER_CRITICAL(&_stateMux);
    const bool accepted = _sensorReady && _state == FP_IDLE;
    if (accepted) _state = FP_CLEARING;
    portEXIT_CRITICAL(&_stateMux);
    if (accepted) setResult("Clearing all fingerprints", "warning");
    return accepted;
}

void FingerprintManager::setCallbacks(void (*onMatch)(int), void (*onNoMatch)()) {
    _onMatch = onMatch;
    _onNoMatch = onNoMatch;
}

uint8_t FingerprintManager::sendCmd8(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3, uint16_t timeoutMs, bool silent) {
    while (_serial.available()) _serial.read();

    _txBuf[0] = CMD_HEAD;
    _txBuf[1] = cmd;
    _txBuf[2] = p1;
    _txBuf[3] = p2;
    _txBuf[4] = p3;
    _txBuf[5] = 0;
    _txBuf[6] = _txBuf[1] ^ _txBuf[2] ^ _txBuf[3] ^ _txBuf[4] ^ _txBuf[5];
    _txBuf[7] = CMD_TAIL;

    _serial.write(_txBuf, 8);
    _serial.flush();

    memset(_rxBuf, 0, sizeof(_rxBuf));
    uint16_t rxCount = 0;
    unsigned long startTime = millis();

    while (millis() - startTime < timeoutMs) {
        if (_serial.available()) {
            uint8_t b = _serial.read();
            if (rxCount == 0 && b != CMD_HEAD) continue;
            _rxBuf[rxCount++] = b;
            if (rxCount >= 8) break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (rxCount < 8) return ACK_TIMEOUT;
    if (_rxBuf[0] != CMD_HEAD || _rxBuf[7] != CMD_TAIL || _rxBuf[1] != cmd) return ACK_FAIL;

    uint8_t chk = _rxBuf[1] ^ _rxBuf[2] ^ _rxBuf[3] ^ _rxBuf[4] ^ _rxBuf[5];
    if (chk != _rxBuf[6]) return ACK_FAIL;

    return ACK_SUCCESS;
}

uint16_t FingerprintManager::getUserCount(bool silent) {
    uint8_t r = sendCmd8(CMD_USER_CNT, 0, 0, 0, 500, silent);
    if (r == ACK_SUCCESS && _rxBuf[4] == ACK_SUCCESS)
        return ((uint16_t)_rxBuf[2] << 8) | _rxBuf[3];
    return 0xFFFF;
}

uint8_t FingerprintManager::setCompareLevel(uint8_t level) {
    uint8_t r = sendCmd8(CMD_COM_LEV, 0, level, 0, 500);
    if (r == ACK_SUCCESS && _rxBuf[4] == ACK_SUCCESS) return _rxBuf[3];
    return 0xFF;
}

bool FingerprintManager::waitReady(uint16_t timeoutMs) {
    uint32_t bauds[] = {19200, 115200, 57600, 38400, 9600};
    unsigned long start = millis();
    
    while (millis() - start < timeoutMs) {
        for (int i = 0; i < 5; i++) {
            _serial.updateBaudRate(bauds[i]);
            vTaskDelay(pdMS_TO_TICKS(50));
            while (_serial.available()) _serial.read();
            
            uint8_t r = setCompareLevel(5);
            if (r == 5) {
                Serial.printf("Fingerprint detected at %lu baud.\n", bauds[i]);
                return true;
            }
            if (millis() - start >= timeoutMs) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    return false;
}

uint8_t FingerprintManager::addUser(uint16_t userId, uint8_t privilege) {
    uint8_t p1 = (uint8_t)((userId >> 8) & 0xFF);
    uint8_t p2 = (uint8_t)(userId & 0xFF);
    uint8_t r;

    r = sendCmd8(CMD_ADD_1, p1, p2, privilege, 10000);
    if (r != ACK_SUCCESS || _rxBuf[4] != ACK_SUCCESS) return (r == ACK_SUCCESS) ? _rxBuf[4] : r;

    r = sendCmd8(CMD_ADD_2, p1, p2, privilege, 10000);
    if (r != ACK_SUCCESS || _rxBuf[4] != ACK_SUCCESS) return (r == ACK_SUCCESS) ? _rxBuf[4] : r;

    r = sendCmd8(CMD_ADD_3, p1, p2, privilege, 10000);
    if (r != ACK_SUCCESS || _rxBuf[4] != ACK_SUCCESS) return (r == ACK_SUCCESS) ? _rxBuf[4] : r;

    return ACK_SUCCESS;
}

uint8_t FingerprintManager::verifyUser() {
    uint8_t r = sendCmd8(CMD_MATCH, 0, 0, 0, 10000);
    if (r == ACK_TIMEOUT) return ACK_TIMEOUT;
    if (r != ACK_SUCCESS) return ACK_FAIL;
    if (_rxBuf[4] == ACK_NO_USER || _rxBuf[4] == ACK_TIMEOUT) return _rxBuf[4];

    _lastUserId = ((uint16_t)_rxBuf[2] << 8) | _rxBuf[3];
    if (_rxBuf[4] >= 1 && _rxBuf[4] <= 3 && _lastUserId != 0) return ACK_SUCCESS;
    return ACK_FAIL;
}

uint8_t FingerprintManager::deleteUserCommand(uint16_t userId) {
    const uint8_t r = sendCmd8(CMD_DEL, userId >> 8, userId & 0xFF, 0, 2000);
    return r == ACK_SUCCESS ? _rxBuf[4] : r;
}

uint8_t FingerprintManager::clearAllCommand() {
    const uint8_t r = sendCmd8(CMD_DEL_ALL, 0, 0, 0, 3000);
    return r == ACK_SUCCESS ? _rxBuf[4] : r;
}

void FingerprintManager::processOperation() {
    uint8_t result;
    const FingerprintState operation = getState();
    if (operation == FP_ADDING) {
        uint16_t newId = _fingerCount + 1;
        result = addUser(newId, ACK_MASTER_USER);
        if (result == ACK_SUCCESS) {
            _retryCount = 0;
            const uint16_t count = getUserCount();
            if (count != 0xFFFF) {
                portENTER_CRITICAL(&_stateMux);
                _fingerCount = count;
                portEXIT_CRITICAL(&_stateMux);
            }
            if (_onMatch) _onMatch(newId);
            char message[48];
            snprintf(message, sizeof(message), "Added fingerprint ID #%u", newId);
            setResult(message, "success");
            setState(FP_IDLE);
        } else if (result == ACK_TIMEOUT || result == ACK_FINGER_OCCUPIED) {
            if (digitalRead(_wakePin) == HIGH && _retryCount < 3) {
                _retryCount++;
            } else {
                _retryCount = 0;
                if (_onNoMatch) _onNoMatch();
                setResult("Fingerprint enrollment timed out", "error");
                setState(FP_IDLE);
            }
        } else {
            setState(FP_IDLE);
            setResult("Fingerprint enrollment failed", "error");
            if (_onNoMatch) _onNoMatch();
        }
    } else if (operation == FP_VERIFYING) {
        result = verifyUser();
        if (result == ACK_SUCCESS) {
            _retryCount = 0;
            if (_onMatch) _onMatch(_lastUserId);
            char message[48];
            snprintf(message, sizeof(message), "Verified fingerprint ID #%u", _lastUserId);
            setResult(message, "success");
            setState(FP_IDLE);
        } else if (result == ACK_TIMEOUT || result == ACK_NO_USER) {
            if (digitalRead(_wakePin) == HIGH && _retryCount < 3) {
                _retryCount++;
            } else {
                _retryCount = 0;
                if (_onNoMatch) _onNoMatch();
                setResult("Fingerprint not recognized", "warning");
                setState(FP_IDLE);
            }
        } else {
            setState(FP_IDLE);
            setResult("Fingerprint verification failed", "error");
            if (_onNoMatch) _onNoMatch();
        }
    } else if (operation == FP_DELETING) {
        result = deleteUserCommand(_pendingUserId);
        if (result == ACK_SUCCESS) {
            const uint16_t count = getUserCount(true);
            if (count != 0xFFFF) {
                portENTER_CRITICAL(&_stateMux);
                _fingerCount = count;
                portEXIT_CRITICAL(&_stateMux);
            }
            char message[48];
            snprintf(message, sizeof(message), "Deleted fingerprint ID #%u", _pendingUserId);
            setResult(message, "success");
        } else {
            setResult("Fingerprint delete failed", "error");
            if (_onNoMatch) _onNoMatch();
        }
        setState(FP_IDLE);
    } else if (operation == FP_CLEARING) {
        result = clearAllCommand();
        if (result == ACK_SUCCESS) {
            portENTER_CRITICAL(&_stateMux);
            _fingerCount = 0;
            portEXIT_CRITICAL(&_stateMux);
            setResult("All fingerprints cleared", "success");
        } else {
            setResult("Fingerprint clear failed", "error");
            if (_onNoMatch) _onNoMatch();
        }
        setState(FP_IDLE);
    }
}

void FingerprintManager::loop(bool saveMode) {
    unsigned long now = millis();
    if (!_sensorReady) {
        const uint32_t retryInterval = saveMode ? SAVE_FINGERPRINT_RETRY_MS
                                                : NORMAL_FINGERPRINT_RETRY_MS;
        if (saveMode && !_saveModePauseLogged) {
            Serial.println("[POWER] Fingerprint detection paused; retry every 600s in SAVE_MODE");
            _saveModePauseLogged = true;
        } else if (!saveMode) {
            _saveModePauseLogged = false;
        }
        if (now - _lastReconnectAttempt < retryInterval) return;
        _lastReconnectAttempt = now;
        setResult("Retrying fingerprint sensor", "warning");
        Serial.println("Retrying fingerprint sensor detection...");
        const bool ready = waitReady(3000);
        _sensorReady = ready;
        if (ready) {
            const uint16_t count = getUserCount(true);
            if (count != 0xFFFF) {
                portENTER_CRITICAL(&_stateMux);
                _fingerCount = count;
                portEXIT_CRITICAL(&_stateMux);
            }
            setResult("Fingerprint sensor reconnected", "success");
            Serial.println("Fingerprint sensor reconnected.");
        } else {
            setResult(saveMode ? "Fingerprint offline; retry in 10min"
                               : "Fingerprint sensor offline; retry in 10s", "error");
        }
        return;
    }
    if (getState() == FP_IDLE && (now - _lastRefresh > 10000)) {
        uint16_t cnt = getUserCount(true);
        if (cnt != 0xFFFF) {
            portENTER_CRITICAL(&_stateMux);
            _fingerCount = cnt;
            portEXIT_CRITICAL(&_stateMux);
        }
        _lastRefresh = now;
    }

    bool isTouching = (digitalRead(_wakePin) == HIGH);
    if (isTouching) {
        _lastTouchTime = now;
        if (_fingerWasRemoved && getState() == FP_IDLE) {
            _fingerWasRemoved = false;
            setState(FP_VERIFYING);
        }
    } else {
        if (now - _lastTouchTime > 300) {
            _fingerWasRemoved = true;
        }
    }

    if (getState() != FP_IDLE) {
        processOperation();
    }
}
