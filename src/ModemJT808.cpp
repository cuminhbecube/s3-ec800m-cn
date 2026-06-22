#include "ModemJT808.h"
#include "Config.h"
#include "EC800Client.h"
#include <time.h>
#include <sys/time.h>

HardwareSerial SerialModem(1);
EC800Client modem(SerialModem);

time_t convertUTCtoEpoch(int year, int month, int day, int hour, int minute, int second) {
    struct tm t = {0};
    t.tm_year = year + 2000 - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    
    // Vì TZ đã được set cứng là UTC0 ở syncSystemTime, mktime sẽ không bị trừ lùi múi giờ
    time_t epoch = mktime(&t);
    return epoch;
}

void syncSystemTime() {
    int yr, mo, dy, hr, mi, se;
    bool has_time = false;
    time_t epoch = 0;
    
    // Cài đặt cứng Múi giờ UTC0 cho toàn hệ thống để mktime tính chuẩn xác epoch từ GNSS/NTP
    setenv("TZ", "UTC0", 1);
    tzset();

    Serial.println("Đang đồng bộ thời gian từ GNSS...");
    if (modem.getGNSSTime(yr, mo, dy, hr, mi, se)) {
        has_time = true;
        epoch = convertUTCtoEpoch(yr, mo, dy, hr, mi, se);
        Serial.println("Đã lấy được thời gian GNSS (UTC)!");
    } else {
        Serial.println("Không có GNSS, thử lấy thời gian qua NTP...");
        const char* servers[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
        bool ntp_ok = false;
        for (int i = 0; i < 3; i++) {
            Serial.printf("Thử kết nối NTP: %s\n", servers[i]);
            if (modem.syncNTP(String(servers[i]))) {
                ntp_ok = true;
                break;
            }
        }
        
        if (ntp_ok) {
            int tz_q = 0;
            if (modem.getNetworkTime(yr, mo, dy, hr, mi, se, tz_q)) {
                has_time = true;
                epoch = convertUTCtoEpoch(yr, mo, dy, hr, mi, se);
                epoch -= (tz_q * 15 * 60);
                Serial.println("Đồng bộ thời gian NTP thành công!");
            }
        }
    }
    
    if (has_time) {
        struct timeval tv;
        tv.tv_sec = epoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        Serial.println("Cập nhật thời gian hệ thống thành công (UTC+7)!");
    } else {
        Serial.println("Đồng bộ thời gian thất bại!");
    }
}

uint16_t jt808_seq = 0;
unsigned long lastSend = 0;

String modem_imei = "";
String modem_ccid = "";
double current_lat = 0.0;
double current_lon = 0.0;
String current_time = "N/A";

unsigned long action_timer = 0;
int init_step = 0;

uint8_t strToBCD(char c1, char c2) {
  uint8_t h = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : 0;
  uint8_t l = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : 0;
  return (h << 4) | l;
}

uint8_t intToBCD(int val) {
    return ((val / 10) << 4) | (val % 10);
}

void getTerminalID(uint8_t* bcd) {
  for(int i = 0; i < 6; i++) {
    bcd[i] = strToBCD(terminal_id[i*2], terminal_id[i*2+1]);
  }
}

int escapeData(uint8_t* in, int in_len, uint8_t* out) {
  int out_len = 0;
  for(int i = 0; i < in_len; i++) {
    if(in[i] == 0x7E) {
      out[out_len++] = 0x7D;
      out[out_len++] = 0x02;
    } else if(in[i] == 0x7D) {
      out[out_len++] = 0x7D;
      out[out_len++] = 0x01;
    } else {
      out[out_len++] = in[i];
    }
  }
  return out_len;
}

void sendJT808Packet(uint16_t msgId, uint8_t* body, uint16_t bodyLen) {
  if (!modem.isTCPConnected()) return;

  uint8_t buf[256];
  int idx = 0;
  
  buf[idx++] = (msgId >> 8) & 0xFF;
  buf[idx++] = msgId & 0xFF;
  
  buf[idx++] = (bodyLen >> 8) & 0xFF;
  buf[idx++] = bodyLen & 0xFF;
  
  uint8_t bcd[6];
  getTerminalID(bcd);
  for(int i=0; i<6; i++) buf[idx++] = bcd[i];
  
  buf[idx++] = (jt808_seq >> 8) & 0xFF;
  buf[idx++] = jt808_seq & 0xFF;
  jt808_seq++;
  
  for(int i=0; i<bodyLen; i++) buf[idx++] = body[i];
  
  uint8_t checksum = 0;
  for(int i=0; i<idx; i++) {
    checksum ^= buf[i];
  }
  buf[idx++] = checksum;
  
  uint8_t out[512];
  out[0] = 0x7E;
  int escLen = escapeData(buf, idx, out + 1);
  out[1 + escLen] = 0x7E;
  
  // Send via EC800
  modem.sendTCPData(out, escLen + 2);
  
  for(int i = 0; i < escLen + 2; i++) {
    Serial.printf("%02X", out[i]);
  }
  Serial.println();
}

void sendAuth() {
  const char authKey[] = "123456";
  sendJT808Packet(0x0102, (uint8_t*)authKey, strlen(authKey));
}

uint32_t convertCoord(double coord) {
  return (uint32_t)(coord * 1000000.0);
}

void sendLocation(double lat, double lon, int y, int m, int d, int h, int min, int s) {
  uint8_t body[28];
  memset(body, 0, sizeof(body));
  
  uint32_t status = 0;
  status |= (1 << 0);
  status |= (1 << 1);
  if (lat < 0) status |= (1 << 2);
  if (lon < 0) status |= (1 << 3);
  
  body[4] = (status >> 24) & 0xFF;
  body[5] = (status >> 16) & 0xFF;
  body[6] = (status >> 8) & 0xFF;
  body[7] = status & 0xFF;
  
  uint32_t lat_dw = convertCoord(abs(lat));
  body[8] = (lat_dw >> 24) & 0xFF;
  body[9] = (lat_dw >> 16) & 0xFF;
  body[10] = (lat_dw >> 8) & 0xFF;
  body[11] = lat_dw & 0xFF;
  
  uint32_t lon_dw = convertCoord(abs(lon));
  body[12] = (lon_dw >> 24) & 0xFF;
  body[13] = (lon_dw >> 16) & 0xFF;
  body[14] = (lon_dw >> 8) & 0xFF;
  body[15] = lon_dw & 0xFF;
  
  body[22] = intToBCD(y);
  body[23] = intToBCD(m);
  body[24] = intToBCD(d);
  body[25] = intToBCD(h);
  body[26] = intToBCD(min);
  body[27] = intToBCD(s);
  
  sendJT808Packet(0x0200, body, 28);
}

void modem_init() {
  modem.begin(115200, EC800_RX, EC800_TX);
  init_step = 0;
}

void modem_loop() {
  unsigned long now = millis();

  // Khởi tạo không dùng delay()
  if (init_step == 0) {
    pinMode(EC800_PWRKEY, OUTPUT);
    pinMode(EC800_RST, OUTPUT);
    digitalWrite(EC800_RST, LOW);
    action_timer = now;
    init_step++;
    return;
  } else if (init_step == 1) {
    if (now - action_timer >= 300) {
      digitalWrite(EC800_RST, HIGH);
      action_timer = now;
      init_step++;
    }
    return;
  } else if (init_step == 2) {
    if (now - action_timer >= 500) {
      digitalWrite(EC800_PWRKEY, HIGH);
      action_timer = now;
      init_step++;
    }
    return;
  } else if (init_step == 3) {
    if (now - action_timer >= 3000) {
      Serial.println("Khởi tạo Modem...");
      if (!modem.initModem()) {
        Serial.println("Lỗi init modem! Sẽ thử lại sau 3s...");
        action_timer = now;
        return;
      }
      init_step++;
    }
    return;
  } else if (init_step == 4) {
    if (!modem.isSimReady()) {
      currentState = STATE_NO_SIM;
      if (now - action_timer >= 3000) {
         Serial.println("Lỗi: Không nhận SIM!");
         action_timer = now;
      }
      return;
    }
    
    modem_imei = modem.getIMEI();
    if (modem_imei.length() >= 12) {
      terminal_id = modem_imei.substring(modem_imei.length() - 12);
    } else {
      terminal_id = "012345678912";
    }
    Serial.println("\n-I-DCE IMEI: " + modem_imei);
    
    modem_ccid = modem.getCCID();
    if (modem_ccid.length() > 0) {
      Serial.println("-I-SimCard CCID: " + modem_ccid);
    }

    Serial.println("Bật GPS...");
    modem.powerOnGPS();

    Serial.println("Đợi mạng 4G/GSM...");
    init_step++;
    return;
  } else if (init_step == 5) {
    if (!modem.isNetworkConnected()) {
      currentState = STATE_NO_NETWORK;
      if (now - action_timer >= 3000) {
        Serial.println("Lỗi mạng, đang chờ...");
        action_timer = now;
      }
      return;
    }
    Serial.println("Kết nối GPRS/APN...");
    init_step++;
    return;
  } else if (init_step == 6) {
    if (!modem.isGPRSConnected()) {
      modem.configGPRS(APN, "", "");
      if (!modem.activateGPRS()) {
        currentState = STATE_NO_INTERNET;
        if (now - action_timer >= 3000) {
          Serial.println("Lỗi APN!");
          action_timer = now;
        }
        return;
      }
    }
    currentState = STATE_NO_GPS;
    Serial.println("Modem đã sẵn sàng kết nối TCP.");
    
    // Thời gian sẽ được đồng bộ động trước khi gửi gói tin
    init_step++;
    return;
  }

  // --- Vòng lặp chính sau khi khởi tạo thành công ---
  static unsigned long last_state_check = 0;
  static bool is_ready_to_send = false;

  // Tránh spam AT commands gây treo thiết bị, chỉ kiểm tra định kỳ mỗi 5s
  if (now - last_state_check >= 5000) {
      last_state_check = now;

      if (modem.isTCPConnected()) {
          is_ready_to_send = true;
          // TCP đã kết nối thì bỏ qua việc check CPIN, CREG, QIACT liên tục để tối ưu luồng AT
      } else {
          is_ready_to_send = false;
          if (!modem.isSimReady()) {
              currentState = STATE_NO_SIM;
              Serial.println("SIM chưa sẵn sàng!");
          } else if (!modem.isNetworkConnected()) {
              currentState = STATE_NO_NETWORK;
              Serial.println("Mất sóng mạng...");
          } else if (!modem.isGPRSConnected()) {
              currentState = STATE_NO_INTERNET;
              Serial.println("Mất GPRS, đang kết nối lại...");
              modem.activateGPRS();
          } else {
              currentState = STATE_NO_INTERNET;
              Serial.printf("Kết nối TCP tới %s:%d...\n", SERVER_IP, SERVER_PORT);
              if (modem.connectTCP(SERVER_IP, SERVER_PORT)) {
                  Serial.println("Kết nối thành công! Gửi gói Auth.");
                  sendAuth(); 
                  is_ready_to_send = true;
              } else {
                  Serial.println("Kết nối thất bại.");
              }
          }
      }
  }

  // Handle URCs / incoming data
  while(modem.stream.available()) {
    String line = modem.stream.readStringUntil('\n');
    line.trim();
    if(line.length() > 0) {
      // In ra để debug (URC, Incoming data)
      Serial.print("URC/Data: ");
      Serial.println(line);
      
      // Nếu có +QIURC: "recv" ta có thể handle ở đây bằng AT+QIRD
      if (line.indexOf("+QIURC: \"recv\"") != -1) {
          modem.sendAT("AT+QIRD=0,1500");
          String rx_data;
          modem.waitResponseStr(1000, rx_data, "OK");
          // Parse rx_data if needed
      }
      if (line.indexOf("+QIURC: \"closed\"") != -1) {
          Serial.println("TCP Server ngắt kết nối!");
          modem.closeTCP();
          is_ready_to_send = false;
          currentState = STATE_NO_INTERNET;
      }
    }
  }

  if (!is_ready_to_send) return;

  // Gửi Location định kỳ
  if (now - lastSend > 10000) {
    lastSend = now;

    double lat = 0.0, lon = 0.0;
    bool hasGPS = modem.getGPSLocation(lat, lon);
    
    if (hasGPS) {
      current_lat = lat;
      current_lon = lon;
      currentState = STATE_HAS_GPS;
      Serial.printf("[GPS] Đã bắt được GPS thật: Lat=%f, Lon=%f\n", current_lat, current_lon);
    } else {
      currentState = STATE_NO_GPS;
      Serial.println("[GPS] Mất sóng GPS, dùng tọa độ mặc định.");
      current_lat = DEFAULT_LAT;
      current_lon = DEFAULT_LON;
    }

    static unsigned long last_sync_try = 0;
    time_t tnow = time(NULL);
    if (tnow < 1000000000 && (now - last_sync_try > 60000 || last_sync_try == 0)) {
        last_sync_try = now;
        syncSystemTime();
        tnow = time(NULL);
    }

    int yr = 24, mo = 6, dy = 15, hr = 12, mi = 0, se = 0;
    if (tnow >= 1000000000) {
        // 1. Hiển thị trên Serial và Web Interface (Giờ Việt Nam UTC+7)
        time_t local_tnow = tnow + 7 * 3600; 
        struct tm *tm_info_local = gmtime(&local_tnow);
        char tbuf[32];
        sprintf(tbuf, "%02d/%02d/20%02d %02d:%02d:%02d", 
                tm_info_local->tm_mday, tm_info_local->tm_mon + 1, tm_info_local->tm_year % 100,
                tm_info_local->tm_hour, tm_info_local->tm_min, tm_info_local->tm_sec);
        current_time = String(tbuf);
        Serial.printf("[TIME] %s (Vietnam Time)\n", tbuf);

        // 2. Giao thức JT808 yêu cầu bắt buộc gửi theo giờ Bắc Kinh (UTC+8)
        // Traccar parse JT808 luôn mặc định chuỗi BCD là giờ UTC+8, nếu ta gửi UTC+7 nó sẽ bị lùi 1 tiếng trên server
        time_t jt808_tnow = tnow + 8 * 3600;
        struct tm *tm_info = gmtime(&jt808_tnow);
        yr = tm_info->tm_year % 100;
        mo = tm_info->tm_mon + 1;
        dy = tm_info->tm_mday;
        hr = tm_info->tm_hour;
        mi = tm_info->tm_min;
        se = tm_info->tm_sec;
    }

    sendLocation(current_lat, current_lon, yr, mo, dy, hr, mi, se);
  }
}
