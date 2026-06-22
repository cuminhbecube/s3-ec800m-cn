#include "EC800Client.h"

EC800Client::EC800Client(HardwareSerial& serial) : stream(serial) {}

void EC800Client::begin(int baud, int rx_pin, int tx_pin) {
    stream.begin(baud, SERIAL_8N1, rx_pin, tx_pin);
}

void EC800Client::clearBuffer() {
    while (stream.available()) {
        stream.read();
    }
}

void EC800Client::sendAT(String cmd) {
    Serial.print("AT TX: ");
    Serial.println(cmd);
    stream.print(cmd + "\r\n");
}

int EC800Client::waitResponseStr(uint32_t timeout_ms, String& response, String expected1, String expected2) {
    unsigned long start = millis();
    response = "";
    while (millis() - start < timeout_ms) {
        if (stream.available()) {
            String line = stream.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) {
                Serial.print("AT RX: ");
                Serial.println(line);
                response += line + "\n";
                
                if (line.indexOf(expected1) != -1) return 1;
                if (expected2 != "" && line.indexOf(expected2) != -1) return 2;
            }
        }
        delay(2);
    }
    return 0; // Timeout
}

int EC800Client::waitResponse(uint32_t timeout_ms, String expected1, String expected2) {
    String dummy;
    return waitResponseStr(timeout_ms, dummy, expected1, expected2);
}

String EC800Client::sendCommandWithResponse(String cmd, uint32_t timeout_ms) {
    sendAT(cmd);
    String resp;
    waitResponseStr(timeout_ms, resp);
    return resp;
}

bool EC800Client::initModem() {
    sendAT("AT");
    if (waitResponse(1000) != 1) return false;
    
    // Tắt echo
    sendAT("ATE0");
    waitResponse(1000);
    
    return true;
}

bool EC800Client::isSimReady() {
    sendAT("AT+CPIN?");
    String resp;
    int res = waitResponseStr(2000, resp, "OK", "ERROR");
    if (res == 1 && resp.indexOf("+CPIN: READY") != -1) return true;
    return false;
}

String EC800Client::getIMEI() {
    sendAT("AT+CGSN");
    String resp;
    waitResponseStr(1000, resp, "OK");
    // Lọc ra số
    String imei = "";
    for (int i = 0; i < resp.length(); i++) {
        if (isDigit(resp[i])) imei += resp[i];
    }
    if (imei.length() > 15) imei = imei.substring(0, 15);
    return imei;
}

String EC800Client::getCCID() {
    sendAT("AT+QCCID");
    String resp;
    waitResponseStr(1000, resp, "OK", "ERROR");
    int idx = resp.indexOf("+QCCID:");
    if (idx != -1) {
        String ccidLine = resp.substring(idx + 7);
        int endIdx = ccidLine.indexOf('\n');
        if (endIdx != -1) ccidLine = ccidLine.substring(0, endIdx);
        ccidLine.trim();
        return ccidLine;
    }
    return "";
}

bool EC800Client::isNetworkConnected() {
    sendAT("AT+CREG?");
    String resp;
    waitResponseStr(2000, resp, "OK");
    if (resp.indexOf("+CREG: 0,1") != -1 || resp.indexOf("+CREG: 0,5") != -1) return true;
    return false;
}

bool EC800Client::configGPRS(String apn, String user, String pass) {
    String cmd = "AT+QICSGP=1,1,\"" + apn + "\",\"" + user + "\",\"" + pass + "\",0";
    sendAT(cmd);
    return waitResponse(2000) == 1;
}

bool EC800Client::activateGPRS() {
    sendAT("AT+QIACT=1");
    // Thời gian timeout có thể lên đến 150s theo tài liệu Quectel
    return waitResponse(10000, "OK", "ERROR") != 0;
}

bool EC800Client::isGPRSConnected() {
    sendAT("AT+QIACT?");
    String resp;
    waitResponseStr(2000, resp, "OK");
    return resp.indexOf("+QIACT: 1,1,1") != -1;
}

bool EC800Client::syncNTP(String server) {
    sendAT("AT+CTZU=3");
    waitResponse(1000);
    String cmd = "AT+QNTP=1,\"" + server + "\",123,1";
    sendAT(cmd);
    String resp;
    if (waitResponseStr(5000, resp, "+QNTP: 0", "+QNTP:") == 1) {
        return true;
    }
    return false;
}

bool EC800Client::getNetworkTime(int &year, int &month, int &day, int &hour, int &minute, int &second, int &tz_quarters) {
    sendAT("AT+CCLK?");
    String resp;
    if (waitResponseStr(1000, resp, "OK", "ERROR") == 1) {
        if (resp.indexOf("+CCLK:") != -1) {
            int idx = resp.indexOf("\"");
            if (idx != -1) {
                String tStr = resp.substring(idx + 1, idx + 18);
                String tzStr = resp.substring(idx + 18, idx + 21);
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
    }
    return false;
}

bool EC800Client::getGNSSTime(int &year, int &month, int &day, int &hour, int &minute, int &second) {
    sendAT("AT+QGPSGNMEA=\"RMC\"");
    String resp;
    if (waitResponseStr(2000, resp, "OK", "ERROR") == 1) {
        int idx = resp.indexOf("$G");
        if (idx != -1) {
            int rmcIdx = resp.indexOf("RMC,", idx);
            if (rmcIdx != -1) {
                int p1 = resp.indexOf(',', rmcIdx);
                int p2 = resp.indexOf(',', p1 + 1);
                int curr = rmcIdx;
                for(int i = 0; i < 8; i++) {
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
    }
    return false;
}

bool EC800Client::connectTCP(String host, int port) {
    closeTCP(); // Ensure closed before opening
    
    // Mở TCP Socket (context=1, connectID=0)
    String cmd = "AT+QIOPEN=1,0,\"TCP\",\"" + host + "\"," + String(port) + ",0,0";
    sendAT(cmd);
    
    // QIOPEN trả về OK ngay, sau đó sẽ báo cáo kết nối bằng URC +QIOPEN: 0,0
    String resp;
    if (waitResponseStr(5000, resp, "+QIOPEN: 0,0", "+QIOPEN: 0,") == 1) {
        tcp_connected = true;
        return true;
    }
    tcp_connected = false;
    return false;
}

bool EC800Client::isTCPConnected() {
    sendAT("AT+QISTATE=1,0");
    String resp;
    waitResponseStr(2000, resp, "OK");
    if (resp.indexOf("+QISTATE: 0,\"TCP\",") != -1) {
        tcp_connected = true;
        return true;
    }
    tcp_connected = false;
    return false;
}

int EC800Client::sendTCPData(const uint8_t* data, size_t len) {
    if (!tcp_connected) return 0;
    
    String cmd = "AT+QISEND=0," + String(len);
    sendAT(cmd);
    
    // Đợi dấu ">"
    unsigned long start = millis();
    bool ready = false;
    while(millis() - start < 2000) {
        if (stream.available()) {
            char c = stream.read();
            if (c == '>') {
                ready = true;
                break;
            }
        }
        delay(2);
    }
    
    if (!ready) {
        Serial.println("Lỗi đợi > QISEND");
        return 0;
    }
    
    // Đẩy data
    stream.write(data, len);
    
    // Đợi SEND OK
    if (waitResponse(5000, "SEND OK", "SEND FAIL") == 1) {
        return len;
    }
    return 0;
}

int EC800Client::availableTCP() {
    if (!tcp_connected) return 0;
    // Kiểm tra xem có dữ liệu đến chưa
    // Việc đọc sẽ dùng lệnh riêng nếu thực sự cần đọc TCP
    // Có thể check buffer của UART nhưng dữ liệu thực nằm trong module
    // Nếu có URC +QIURC: "recv",0 thì ta sẽ đọc
    return stream.available(); // Dummy, để handle URC trong loop
}

int EC800Client::readTCPData(uint8_t* buffer, size_t max_len) {
    // Để đọc tcp, lệnh: AT+QIRD=0,<len>
    // Module trả về: +QIRD: <len>\r\n<data>\r\nOK
    // Tạm thời bỏ qua phần nhận data lớn, ưu tiên check URC trước
    return 0;
}

void EC800Client::closeTCP() {
    sendAT("AT+QICLOSE=0,5000");
    waitResponse(5000);
    tcp_connected = false;
}

bool EC800Client::powerOnGPS() {
    sendAT("AT+QGPS=1");
    return waitResponse(2000) == 1; // Có thể ERROR nếu đã bật
}

bool EC800Client::powerOffGPS() {
    sendAT("AT+QGPSEND");
    return waitResponse(2000) == 1;
}

bool EC800Client::getGPSLocation(double& lat, double& lon) {
    sendAT("AT+QGPSLOC=1");
    String resp;
    int res = waitResponseStr(3000, resp, "OK", "ERROR");
    
    if (res == 1 && resp.indexOf("+QGPSLOC:") != -1) {
        // format: +QGPSLOC: 063816.0,20.99010,105.85700,2.1,10.0,3,149.27,0.0,0.0,261122,05
        int p1 = resp.indexOf(',');
        int p2 = resp.indexOf(',', p1+1);
        int p3 = resp.indexOf(',', p2+1);
        
        if (p1 != -1 && p2 != -1 && p3 != -1) {
            String latStr = resp.substring(p1+1, p2);
            String lonStr = resp.substring(p2+1, p3);
            lat = latStr.toDouble();
            lon = lonStr.toDouble();
            if (lat != 0.0 && lon != 0.0) return true;
        }
    }
    return false;
}
