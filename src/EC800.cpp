#include "EC800.h"
#include <time.h>

EC800::EC800(HardwareSerial& serial, int pwrkeyPin, int rstPin) 
    : _serial(serial), _pwrkeyPin(pwrkeyPin), _rstPin(rstPin) {
}

void EC800::begin(unsigned long baudrate, int rxPin, int txPin) {
    if (_pwrkeyPin >= 0) {
        pinMode(_pwrkeyPin, OUTPUT);
        digitalWrite(_pwrkeyPin, HIGH); // Always high as requested
    }
    if (_rstPin >= 0) {
        pinMode(_rstPin, OUTPUT);
        digitalWrite(_rstPin, LOW); // Default low
    }
    _serial.begin(baudrate, SERIAL_8N1, rxPin, txPin);
}

bool EC800::init() {
    _networkRegistrationStatus = -1;
    _networkRegistrationChanged = false;
    drainInput();
    // Disable echo
    sendATCommand("ATE0", 1000);
    // Check communication
    const String response = sendATCommand("AT", 1000);
    if (response.indexOf("OK") == -1) return false;

    // Enable EPS registration URCs. CEREG is an observation mechanism only;
    // it does not keep PDP or TCP sessions alive.
    const String ceregResponse = sendATCommand("AT+CEREG=1", 1000);
    if (ceregResponse.indexOf("OK") == -1) return false;
    
    return true;
}

bool EC800::getIMEI(String& imei) {
    String resp = sendATCommand("AT+GSN", 1000);
    if (resp != "") {
        String current_imei = "";
        for (unsigned int i = 0; i < resp.length(); i++) {
            if (isDigit(resp[i])) {
                current_imei += resp[i];
                if (current_imei.length() == 15) {
                    imei = current_imei;
                    return true;
                }
            } else {
                current_imei = "";
            }
        }
    }
    return false;
}

bool EC800::getCCID(String& ccid) {
    String resp = sendATCommand("AT+QCCID", 2000);
    int idx = resp.indexOf("+QCCID: ");
    if (idx != -1) {
        String data = resp.substring(idx + 8);
        data.replace("\r", "");
        data.replace("\n", "");
        int endIdx = data.indexOf("OK");
        if(endIdx != -1) {
            data = data.substring(0, endIdx);
            data.trim();
        }
        if (data.length() > 0) {
            ccid = data;
            return true;
        }
    }
    return false;
}

bool EC800::enableGPS() {
    String resp = sendATCommand("AT+QGPS=1", 2000); // Enable GPS
    // It might return OK, or +CME ERROR: 504 if already running
    return (resp.indexOf("OK") != -1 || resp.indexOf("504") != -1);
}

bool EC800::disableGPS() {
    const String resp = sendATCommand("AT+QGPSEND", 2000);
    // Some firmware reports an error when GNSS is already stopped.
    return resp.indexOf("OK") != -1 || resp.indexOf("504") != -1;
}

bool EC800::restartGPS() {
    // QGPSEND can report an error when GNSS is already stopped; enabling it
    // afterwards is the authoritative result.
    sendATCommand("AT+QGPSEND", 2000);
    delay(250);
    return enableGPS();
}

// Convert "ddmm.mmmm" to dd.dddddd
float parseDegreeMinute(const String& dm) {
    if (dm.length() < 4) return 0.0;
    int dotIndex = dm.indexOf('.');
    if (dotIndex < 2) return 0.0;
    
    float degrees = dm.substring(0, dotIndex - 2).toFloat();
    float minutes = dm.substring(dotIndex - 2).toFloat();
    return degrees + (minutes / 60.0);
}

static int64_t utcEpoch(int year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    // Gregorian civil date to Unix epoch, independent of process TZ.
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

bool EC800::parseGPS(float& lat, float& lon, float& speed, float& course, float& alt,
                     uint8_t& sats, float& pdop, float& hdop, uint64_t& utcTime, bool& isValid) {
    String resp = sendATCommand("AT+QGPSGNMEA=\"RMC\"", 2000);
    
    speed = 0.0;
    course = 0.0;
    alt = 0.0;
    sats = 0;
    pdop = 0.0;
    hdop = 0.0;
    isValid = false;
    
    if (resp.indexOf("OK") != -1) {
        int rmcIdx = resp.indexOf("RMC,");
        if (rmcIdx != -1) {
            // RMC format: $xxRMC,hhmmss.ss,A,lat,N,lon,E,speed,course,ddmmyy,...
            // Fields after "RMC,": [0]=time, [1]=status, [2]=lat, [3]=N/S, [4]=lon, [5]=E/W, [6]=speed, [7]=course, [8]=date
            int p[12];
            memset(p, -1, sizeof(p));
            int curr = rmcIdx + 3; // point to comma before time
            for (int i = 0; i < 11; i++) {
                curr = resp.indexOf(',', curr + 1);
                if (curr == -1) break;
                p[i] = curr;
            }
            if (p[6] != -1) {
                String timeStr = resp.substring(rmcIdx + 4, p[0]); // hhmmss.ss
                String status = resp.substring(p[0] + 1, p[1]);
                if (status == "A") { // Active
                    String latStr = resp.substring(p[1] + 1, p[2]);
                    String ns = resp.substring(p[2] + 1, p[3]);
                    String lonStr = resp.substring(p[3] + 1, p[4]);
                    String ew = resp.substring(p[4] + 1, p[5]);
                    
                    if (latStr.length() >= 4 && lonStr.length() >= 5) {
                        lat = parseDegreeMinute(latStr);
                        if (ns == "S") lat = -lat;
                        
                        lon = parseDegreeMinute(lonStr);
                        if (ew == "W") lon = -lon;
                        
                        // Parse speed in knots → km/h
                        if (p[6] != -1) {
                            String speedStr = resp.substring(p[5] + 1, p[6]);
                            speedStr.trim();
                            if (speedStr.length() > 0) speed = speedStr.toFloat() * 1.852;
                        }
                        
                        // Parse course in degrees
                        if (p[7] != -1) {
                            String courseStr = resp.substring(p[6] + 1, p[7]);
                            courseStr.trim();
                            if (courseStr.length() > 0) course = courseStr.toFloat();
                        }
                        
                        // Parse UTC time from RMC time + date fields
                        String dateStr = (p[8] != -1) ? resp.substring(p[7] + 1, p[8]) : "";
                        if (timeStr.length() >= 6 && dateStr.length() >= 6) {
                            struct tm t = {0};
                            t.tm_hour = timeStr.substring(0, 2).toInt();
                            t.tm_min  = timeStr.substring(2, 4).toInt();
                            t.tm_sec  = timeStr.substring(4, 6).toInt();
                            t.tm_mday = dateStr.substring(0, 2).toInt();
                            t.tm_mon  = dateStr.substring(2, 4).toInt() - 1;
                            t.tm_year = dateStr.substring(4, 6).toInt() + 2000 - 1900;
                            // FIX-CRIT-03: RMC is UTC. utcEpoch is independent
                            // of the device display timezone (UTC+7).
                            utcTime = static_cast<uint64_t>(utcEpoch(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                                                                      t.tm_hour, t.tm_min, t.tm_sec)) * 1000ULL;
                        } else {
                            utcTime = (uint64_t)time(NULL) * 1000ULL; // Fallback
                        }
                        
                        // Lấy thêm GGA để đọc Số vệ tinh, HDOP và Độ cao (Altitude)
                        String ggaResp = sendATCommand("AT+QGPSGNMEA=\"GGA\"", 2000);
                        if (ggaResp.indexOf("OK") != -1) {
                            int ggaIdx = ggaResp.indexOf("GGA,");
                            if (ggaIdx != -1) {
                                int pG[15];
                                memset(pG, -1, sizeof(pG));
                                int currG = ggaIdx + 3;
                                for (int i = 0; i < 14; i++) {
                                    currG = ggaResp.indexOf(',', currG + 1);
                                    if (currG == -1) break;
                                    pG[i] = currG;
                                }
                                if (pG[6] != -1) {
                                    String satsStr = ggaResp.substring(pG[5] + 1, pG[6]);
                                    satsStr.trim();
                                    if (satsStr.length() > 0) sats = satsStr.toInt();
                                }
                                if (pG[7] != -1) {
                                    String hdopStr = ggaResp.substring(pG[6] + 1, pG[7]);
                                    hdopStr.trim();
                                    if (hdopStr.length() > 0) hdop = hdopStr.toFloat();
                                }
                                if (pG[8] != -1) {
                                    String altStr = ggaResp.substring(pG[7] + 1, pG[8]);
                                    altStr.trim();
                                    if (altStr.length() > 0) alt = altStr.toFloat();
                                }
                            }
                        }

                        // GSA carries PDOP, which was previously always zero.
                        String gsaResp = sendATCommand("AT+QGPSGNMEA=\"GSA\"", 2000);
                        int gsaIdx = gsaResp.indexOf("GSA,");
                        if (gsaIdx != -1) {
                            int lineEnd = gsaResp.indexOf('\r', gsaIdx);
                            String fieldsText = gsaResp.substring(gsaIdx + 4, lineEnd == -1 ? gsaResp.length() : lineEnd);
                            std::vector<String> fields = splitString(fieldsText, ',');
                            if (fields.size() >= 16) {
                                pdop = fields[14].toFloat();
                                if (hdop <= 0.0f) hdop = fields[15].toFloat();
                            }
                        }
                        
                        // RMC=A alone is not enough for the UI/AVL fix flag when
                        // GGA reports no satellites (or could not be parsed).
                        isValid = sats > 0;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

int EC800::getNetworkRegistrationStatus() {
    String resp = sendATCommand("AT+CEREG?", 1000);
    return resp.indexOf("+CEREG:") != -1 ? _networkRegistrationStatus : -1;
}

bool EC800::takeNetworkRegistrationUrc(int& stat) {
    if (!_networkRegistrationChanged) return false;
    stat = _networkRegistrationStatus;
    _networkRegistrationChanged = false;
    return true;
}

bool EC800::getNetworkTime(int& year, int& month, int& day, int& hour, int& minute, int& second, int& tz_quarters) {
    String resp = sendATCommand("AT+CCLK?", 1000);
    // +CCLK: "24/06/24,17:53:50+28"
    int idx = resp.indexOf("+CCLK:");
    if (idx != -1) {
        int qIdx = resp.indexOf("\"", idx);
        if (qIdx != -1) {
            String tStr = resp.substring(qIdx + 1, qIdx + 18);
            String tzStr = resp.substring(qIdx + 18, qIdx + 21);
            if (tStr.length() >= 17) {
                year = tStr.substring(0, 2).toInt();
                month = tStr.substring(3, 5).toInt();
                day = tStr.substring(6, 8).toInt();
                hour = tStr.substring(9, 11).toInt();
                minute = tStr.substring(12, 14).toInt();
                second = tStr.substring(15, 17).toInt();
                tz_quarters = tzStr.toInt();
                return true;
            }
        }
    }
    return false;
}

bool EC800::getGNSSTime(int &year, int &month, int &day, int &hour, int &minute, int &second) {
    String resp = sendATCommand("AT+QGPSGNMEA=\"RMC\"", 2000);
    if (resp.indexOf("OK") != -1) {
        int rmcIdx = resp.indexOf("RMC,");
        if (rmcIdx != -1) {
            int p1 = resp.indexOf(',', rmcIdx);
            int p2 = resp.indexOf(',', p1 + 1);
            int curr = rmcIdx;
            for(int i = 0; i < 9; i++) {
                curr = resp.indexOf(',', curr + 1);
                if (curr == -1) break;
            }
            if (curr != -1) {
                int p9 = curr;
                int p10 = resp.indexOf(',', p9 + 1);
                if (p10 != -1) {
                    String timeStr = resp.substring(p1 + 1, p2);
                    String dateStr = resp.substring(p9 + 1, p10);
                    if (timeStr.length() >= 6 && dateStr.length() >= 6) {
                        hour = timeStr.substring(0, 2).toInt();
                        minute = timeStr.substring(2, 4).toInt();
                        second = timeStr.substring(4, 6).toInt();
                        day = dateStr.substring(0, 2).toInt();
                        month = dateStr.substring(2, 4).toInt();
                        year = dateStr.substring(4, 6).toInt();
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool EC800::syncNTP(String server, int &year, int &month, int &day, int &hour, int &minute, int &second) {
    sendATCommand("AT+CTZU=3", 1000);
    String cmd = "AT+QNTP=1,\"" + server + "\",123,1";
    
    // QNTP returns OK immediately, then +QNTP: 0,"time" URC later
    // sendATCommand with expectedResponse
    String resp = sendATCommand(cmd, 8000, "+QNTP: 0");
    if (resp.indexOf("+QNTP: 0,\"") != -1) {
        int idx = resp.indexOf("+QNTP: 0,\"");
        String tStr = resp.substring(idx + 10);
        if (tStr.length() >= 19) {
            year = tStr.substring(2, 4).toInt();
            month = tStr.substring(5, 7).toInt();
            day = tStr.substring(8, 10).toInt();
            hour = tStr.substring(11, 13).toInt();
            minute = tStr.substring(14, 16).toInt();
            second = tStr.substring(17, 19).toInt();
            return true;
        }
    }
    return false;
}

int EC800::getRSSI() {
    String resp = sendATCommand("AT+CSQ", 1000);
    int csqIndex = resp.indexOf("+CSQ: ");
    if (csqIndex != -1) {
        String csqStr = resp.substring(csqIndex + 6);
        int commaIndex = csqStr.indexOf(',');
        if (commaIndex != -1) {
            int rssiVal = csqStr.substring(0, commaIndex).toInt();
            if (rssiVal == 99) return 0;
            // Map 0-31 to dBm
            return -113 + (rssiVal * 2);
        }
    }
    return 0;
}

bool EC800::connectTCP(const String& host, uint16_t port) {
    // Close existing just in case
    sendATCommand("AT+QICLOSE=0", 2000);
    
    // access_mode = 0 (Buffer Access Mode) to allow using AT+QIRD
    String cmd = "AT+QIOPEN=1,0,\"TCP\",\"" + host + "\"," + String(port) + ",0,0";
    Serial.println("AT TX: " + cmd);
    _serial.println(cmd);
    
    unsigned long start = millis();
    String currentLine = "";
    String response = "";
    
    while (millis() - start < 15000) {
        while (_serial.available()) {
            char c = _serial.read();
            response += c;
            if (c == '\n') {
                String printLine = currentLine;
                printLine.replace("\r", "");
                if (printLine.length() > 0) Serial.println("AT RX: " + printLine);
                handleModemLine(printLine);
                currentLine = "";
            } else {
                currentLine += c;
            }
        }
        
        if (response.indexOf("+QIOPEN: 0,0") != -1) {
            return true;
        }
        // If it's +QIOPEN: 0,X where X is not 0 (meaning error)
        if (response.indexOf("+QIOPEN: 0,") != -1 && response.indexOf("+QIOPEN: 0,0") == -1) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    return false;
}

bool EC800::closeTCP() {
    sendATCommand("AT+QICLOSE=0", 2000);
    return true;
}

bool EC800::sendTCP(const std::vector<uint8_t>& data) {
    String cmd = "AT+QISEND=0," + String(data.size());
    Serial.println("AT TX: " + cmd);
    _serial.println(cmd);
    
    // Wait for '>'
    unsigned long start = millis();
    bool ready = false;
    while (millis() - start < 2000) {
        if (_serial.available()) {
            char c = _serial.read();
            if (c == '>') {
                Serial.println("AT RX: >");
                ready = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (!ready) {
        Serial.println("AT RX: [TIMEOUT waiting for >]");
        return false;
    }
    
    Serial.print("AT TX: [BINARY DATA len=");
    Serial.print(data.size());
    Serial.print("] HEX: ");
    for (size_t i = 0; i < data.size(); i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
    }
    Serial.println();
    _serial.write(data.data(), data.size());
    
    String resp = "";
    start = millis();
    String currentLine = "";
    while (millis() - start < 5000) {
        if (_serial.available()) {
            char c = _serial.read();
            resp += c;
            if (c == '\n') {
                String printLine = currentLine;
                printLine.replace("\r", "");
                if (printLine.length() > 0) Serial.println("AT RX: " + printLine);
                handleModemLine(printLine);
                if (printLine.indexOf("SEND OK") != -1) {
                    currentLine = ""; // Reset before return to avoid double-print
                    return true;
                }
                if (printLine.indexOf("SEND FAIL") != -1) return false;
                currentLine = "";
            } else {
                currentLine += c;
            }
        }
    }
    if (currentLine.length() > 0) Serial.println("AT RX: " + currentLine);
    return false;
}

bool EC800::readTCP(std::vector<uint8_t>& data, uint32_t timeoutMs, size_t maxBytes) {
    data.clear();
    if (maxBytes == 0 || maxBytes > 1500) maxBytes = 1500;
    // Wait for URC first
    unsigned long startURC = millis();
    bool urcReceived = _tcpDataPending;
    _tcpDataPending = false;
    String currentLine = "";
    
    while (!urcReceived && millis() - startURC < timeoutMs) {
        if (_serial.available()) {
            char c = _serial.read();
            if (c == '\n') {
                String printLine = currentLine;
                printLine.replace("\r", "");
                if (printLine.length() > 0) Serial.println("AT RX: " + printLine);
                handleModemLine(printLine);
                if (printLine.indexOf("+QIURC: \"recv\"") != -1) {
                    urcReceived = true;
                    currentLine = ""; // Reset before break to avoid double-print
                    break;
                }
                currentLine = "";
            } else {
                currentLine += c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    if (currentLine.length() > 0) Serial.println("AT RX: " + currentLine);
    
    if (!urcReceived) {
        return false;
    }
    
    // FIX-CRIT-09: Read exactly the protocol unit expected by the caller for
    // login/AVL ACKs, preventing a coalesced Codec12 command from being consumed.
    String cmd = "AT+QIRD=0," + String(maxBytes);
    Serial.println("AT TX: " + cmd);
    _serial.println(cmd);
    
    unsigned long start = millis();
    String header = "";
    int expectedLength = 0;
    bool readingData = false;
    
    while (millis() - start < timeoutMs) {
        while (_serial.available()) {
            if (!readingData) {
                char c = _serial.read();
                header += c;
                if (header.endsWith("\r\n")) {
                    String printLine = header;
                    printLine.replace("\r", ""); printLine.replace("\n", "");
                    if (printLine.length() > 0) Serial.println("AT RX: " + printLine);
                    handleModemLine(printLine);

                    if (header.indexOf("+QIRD:") != -1) {
                        int spaceIdx = header.indexOf(' ');
                        int rIdx = header.indexOf('\r', spaceIdx);
                        if (spaceIdx != -1 && rIdx != -1) {
                            expectedLength = header.substring(spaceIdx + 1, rIdx).toInt();
                            if (expectedLength > 0) {
                                readingData = true;
                                header = "";
                            } else {
                                return false; // 0 length means no data
                            }
                        }
                    } else if (header.indexOf("OK\r\n") != -1 && expectedLength == 0) {
                        return false;
                    } else {
                        header = ""; // Reset for next line
                    }
                }
            } else {
                data.push_back(_serial.read());
                if (data.size() == expectedLength) {
                    Serial.print("AT RX: [BINARY DATA len=");
                    Serial.print(expectedLength);
                    Serial.println("]");
                    // Note: trailing \r\nOK\r\n is left in buffer, will be cleared by next sendATCommand
                    return true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

void EC800::hwReset() {
    if (_rstPin >= 0) {
        digitalWrite(_rstPin, HIGH);
        vTaskDelay(pdMS_TO_TICKS(300));
        digitalWrite(_rstPin, LOW);
        vTaskDelay(pdMS_TO_TICKS(3000)); // Wait for module to boot after reset
    }
}

String EC800::sendATCommand(const String& command, uint32_t timeout, const String& expectedResponse) {
    drainInput();
    Serial.println("AT TX: " + command);
    _serial.println(command);
    
    String response = "";
    unsigned long start = millis();
    String currentLine = "";
    
    while (millis() - start < timeout) {
        while (_serial.available()) {
            char c = _serial.read();
            response += c;
            
            if (c == '\n') {
                String printLine = currentLine;
                printLine.replace("\r", "");
                if (printLine.length() > 0) {
                    Serial.println("AT RX: " + printLine);
                }
                handleModemLine(printLine);
                currentLine = "";
            } else {
                currentLine += c;
            }
        }
        
        if (expectedResponse != "OK" && response.indexOf(expectedResponse) != -1) {
            // Wait a bit more for the rest of the line
            vTaskDelay(pdMS_TO_TICKS(50));
            while (_serial.available()) {
                char c = _serial.read();
                response += c;
                if (c == '\n') {
                    String printLine = currentLine;
                    printLine.replace("\r", "");
                    if (printLine.length() > 0) Serial.println("AT RX: " + printLine);
                    handleModemLine(printLine);
                    currentLine = "";
                } else {
                    currentLine += c;
                }
            }
            if (currentLine.length() > 0) Serial.println("AT RX: " + currentLine);
            if (response.indexOf("+QIURC: \"recv\"") != -1) _tcpDataPending = true;
            return response;
        }
        
        const bool terminalError = response.indexOf("ERROR\r\n") != -1;
        const bool defaultComplete = expectedResponse == "OK" && response.indexOf("OK\r\n") != -1;
        // FIX-CRIT-03: Async commands such as QNTP return an immediate OK and
        // their real result later as a URC. Do not return early for custom expectations.
        if (defaultComplete || terminalError) {
            if (currentLine.length() > 0) Serial.println("AT RX: " + currentLine);
            if (response.indexOf("+QIURC: \"recv\"") != -1) _tcpDataPending = true;
            return response;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (currentLine.length() > 0) Serial.println("AT RX: " + currentLine);
    if (response == "") Serial.println("AT RX: [TIMEOUT]");
    if (response.indexOf("+QIURC: \"recv\"") != -1) _tcpDataPending = true;
    return response; 
}

void EC800::drainInput() {
    // FIX-CRIT-09: Drain only stale textual modem responses and remember TCP
    // receive URCs instead of silently deleting server commands/ACK notices.
    String drained;
    unsigned long lastByte = millis();
    while (_serial.available() || millis() - lastByte < 20) {
        while (_serial.available()) {
            const char c = _serial.read();
            if (drained.length() < 512) drained += c;
            lastByte = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (drained.indexOf("+QIURC: \"recv\"") != -1) _tcpDataPending = true;
    int lineStart = 0;
    while (lineStart < static_cast<int>(drained.length())) {
        int lineEnd = drained.indexOf('\n', lineStart);
        if (lineEnd == -1) lineEnd = drained.length();
        String line = drained.substring(lineStart, lineEnd);
        line.replace("\r", "");
        handleModemLine(line);
        lineStart = lineEnd + 1;
    }
}

void EC800::handleModemLine(const String& line) {
    const int prefix = line.indexOf("+CEREG:");
    if (prefix == -1) return;

    String fields = line.substring(prefix + 7);
    fields.trim();
    const int comma = fields.indexOf(',');
    // With CEREG=1, a query is <n>,<stat>, while a URC is just <stat>.
    String statText = comma == -1 ? fields : fields.substring(comma + 1);
    const int nextComma = statText.indexOf(',');
    if (nextComma != -1) statText = statText.substring(0, nextComma);
    statText.trim();
    if (statText.length() == 0 || !isDigit(statText[0])) return;

    const int stat = statText.toInt();
    if (stat < 0 || stat > 5) return;
    if (_networkRegistrationStatus != stat) {
        _networkRegistrationStatus = stat;
        _networkRegistrationChanged = true;
    }
}

std::vector<String> EC800::splitString(const String& str, char delimiter) {
    std::vector<String> result;
    int start = 0;
    int end = str.indexOf(delimiter);
    while (end != -1) {
        result.push_back(str.substring(start, end));
        start = end + 1;
        end = str.indexOf(delimiter, start);
    }
    result.push_back(str.substring(start));
    return result;
}
