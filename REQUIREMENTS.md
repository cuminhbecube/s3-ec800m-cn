# ĐẶC TẢ PHẦN CỨNG (HARDWARE SPECIFICATIONS)
- **MCU:** ESP32-S3 (Module: ESP32-S3-WROOM-N16R8)
- **Module 4G/GPS:** Quectel EC800
  - UART: TX = GPIO4, RX = GPIO5
  - Chức năng: TCP Client, GPS Location, Cell Information, Network Status
- **Cảm biến vân tay:**
  - UART2: TX = GPIO9, RX = GPIO10
  - Wake Pin: GPIO11
- **Nguồn (Power):**
  - Đo điện áp pin (VBAT ADC): GPIO17
  - Tín hiệu khóa điện (ACC): GPIO3
- **Chỉ báo (Alarm/LED):**
  - LED trạng thái: GPIO21
  - NeoPixel (1 LED RGB): GPIO47
  - Buzzer: GPIO41

# ĐẶC TẢ TÍNH NĂNG (FUNCTIONAL REQUIREMENTS)

## 1. Chức năng GPS
- Lấy dữ liệu từ EC800 bao gồm: Latitude, Longitude, Speed, Course, Altitude, Satellite Count, HDOP, UTC Time.
- Chỉ đánh dấu `GPS Valid` khi GPS đã thực sự FIX.

## 2. Giao thức Teltonika Codec8E
- **Login:** Sau khi kết nối TCP thành công, lấy IMEI từ EC800 và gửi theo chuẩn Teltonika. Chờ ACK = `0x01`. Nếu lỗi ACK, tiến hành reconnect.
- **AVL Data:** Gửi dữ liệu Codec8E. Một Record phải chứa thông tin GPS và các IO Elements mapping như sau:
  - AVL 239 = Ignition (ACC)
  - AVL 66 = Battery Voltage (mV)
  - AVL 67 = Battery Level (%)
  - AVL 70 = Temperature
  - AVL 240 = GSM Signal
  - AVL 1 = Digital Input ACC
  - AVL 179 = Total Mileage
  - AVL 181 = GNSS PDOP
  - AVL 182 = GNSS HDOP

## 3. Quản lý Backlog (Lưu trữ ngoại tuyến)
- Thiết bị phải lưu lịch sử khi mất kết nối mạng.
- Sử dụng LittleFS hoặc SPIFFS (Không dùng EEPROM).
- Giới hạn lưu trữ: Tối đa 5000 records.
- **Logic hoạt động:**
  - Khi mất TCP: GPS vẫn tiếp tục lấy dữ liệu và ghi record vào Flash.
  - Khi có mạng lại: Ưu tiên gửi backlog trước, sau đó mới gửi dữ liệu realtime.
  - Cho phép cấu hình gom số lượng record gửi đi mỗi packet bằng biến compile-time (ví dụ: gom 5, 10, hoặc 20 records/packet).

## 4. ACK Logic & Retry
- Sau khi gửi AVL packet, Server sẽ trả về `ACK = Number Of Records`.
  - VD: Gửi 5 Records -> Nhận ACK = 5 -> Xóa 5 record khỏi Flash queue.
- Nếu ACK sai hoặc bị timeout: Gửi lại packet. Tối đa retry 3 lần.

## 5. Command Channel & Response
- Hỗ trợ nhận command từ Server (Traccar).
- Các lệnh hỗ trợ: `config`, `status`, `reboot`, `restart`, `device_restart`, `beep`, `led,on`, `led,off`, `gps`, `fingerprint`, `factory_reset`, `set_interval,60`, `set_server,host,port`, `acc1`, `acc0`, `acc?`, `acc_mode,physical`, `acc_mode,virtual`, `force_send`, `reconnect`, `gnss_restart`, `backlog`, `set_batch,6`, `set_sample,10`.
  - `acc1` / `acc0` chuyển sang chế độ ACC ảo, bật/tắt trạng thái khóa và đồng bộ với Web Dashboard.
  - Lệnh ACC không phân biệt chữ hoa/chữ thường (`acc1`, `Acc1`, `ACC1` đều hợp lệ).
  - `factory_reset` chỉ cấp mã xác nhận có hiệu lực 60 giây; reset chỉ chạy với `factory_reset,<code>` và giữ nguyên backlog.
  - Khi ACC OFF, thiết bị dùng `power_save`: giảm GNSS polling và gửi tối đa một status record mỗi 300 giây. Khi ACC ON, sampling/batching dùng cấu hình persistent `set_sample` và `set_batch`.
- Phản hồi server bằng Codec8E Command Response. VD: Lệnh `config` trả về dạng text:
  `FW=1.0.0, IMEI=xxx, VBAT=4.15, RSSI=20, ACC=1, GPS=FIX, INTERVAL=60`

## 6. Fingerprint (Cảm biến vân tay)
- Triển khai driver trên UART2.
- Các chức năng: Detect Finger, Match Finger, Enroll Finger, Delete Finger.
- Bắt buộc có callbacks: `onFingerprintMatched()` và `onFingerprintNotMatched()`.

## 7. Alarm & Indicators
- **Buzzer:** 2 beep khi boot, 1 beep khi GPS fix, 3 beep khi quét vân tay thành công.
- **NeoPixel:**
  - Đỏ: Mất mạng
  - Xanh lá: GPS Fix
  - Xanh dương: Đang kết nối Server
  - Vàng: Đang upload Backlog

## 8. Power & System Watchdog
- Đọc VBAT từ GPIO17 qua thuật toán lọc Kalman hoặc EMA, sau đó chuyển đổi ra Voltage và Battery Percent.
- **Watchdog:** Nếu TCP bị treo hoặc EC800 không phản hồi -> Tự động reset modem. Nếu reset modem thất bại -> Gọi `ESP.restart()`.

# YÊU CẦU ĐẦU RA (DELIVERABLES)
Agent cần tạo đầy đủ:
- `platformio.ini`
- Cấu trúc thư mục (folder structure)
- Source code hoàn chỉnh
- Codec8E encoder & decoder
- AVL record builder
- Flash queue manager
- Command parser
- EC800 driver & Fingerprint driver
- UML architecture & State machine diagram (dạng text hoặc Mermaid)
- `README.md`
