# S3 GPS Tracker (JT808)

Dự án thiết bị định vị GPS 4G dành cho ô tô/xe máy, sử dụng vi điều khiển **ESP32-S3** kết hợp cùng module 4G/GPS **Quectel EC800**. Hệ thống truyền dữ liệu vị trí về máy chủ Traccar bằng giao thức chuẩn **JT808** (Huabao).

---

## 🌟 Các Tính Năng Nổi Bật

1. **Giao thức chuẩn JT808:**
   - Kết nối máy chủ TCP tin cậy (Mặc định: `your-server.com:5015`).
   - Tự động nhận diện Terminal ID từ 12 số cuối của chuỗi IMEI.
   - Hỗ trợ gói tin xác thực (Auth Packet) và gói báo cáo tọa độ định kỳ (10 giây/lần).
   - **Xử lý Múi giờ Độc lập:** Hệ thống tự động phân tách 2 luồng thời gian. Thời gian giao tiếp với máy chủ JT808 được tự động ép về **Giờ Bắc Kinh (UTC+8)** theo đúng tiêu chuẩn bắt buộc của giao thức, trong khi Web UI và Log nội bộ vẫn hiển thị **Giờ Việt Nam (UTC+7)**, giúp loại bỏ hoàn toàn hiện tượng lệch 1 giờ trên bản đồ Server.

2. **Cơ chế Khôi phục Thời gian (NTP & GNSS):**
   - Thiết bị ưu tiên lấy thời gian cực kỳ chính xác từ Vệ Tinh (GNSS/RMC).
   - Trong trường hợp xe đậu trong tầng hầm mất sóng vệ tinh, thiết bị sẽ tự động dò và đồng bộ thời gian từ mạng di động qua **NTP** (`pool.ntp.org`, `time.google.com`, `time.cloudflare.com`) để đảm bảo không bao giờ bị sai ngày giờ gửi lên server.

3. **Web Dashboard Tích Hợp (Offline):**
   - Thiết bị tự động phát Wi-Fi Access Point mang tên `S3_GPS_Tracker`.
   - Giao diện Web siêu hiện đại (UI Glassmorphism, Dark mode, No-scroll mobile first).
   - Truy cập qua địa chỉ: **http://192.168.4.1** để giám sát trực tiếp:
     - Tọa độ GPS (Lat/Lon) thực tế.
     - Thời gian đồng bộ chuẩn trên mạch (Device Time).
     - Trạng thái kết nối (Khởi tạo, Lỗi SIM, Dò sóng, Online).
     - ID Thiết bị, Mã IMEI (Modem) & CCID (Simcard).
     - Bộ nhớ RAM khả dụng (Free Heap) theo thời gian thực (1s/lần).

4. **Chỉ Báo Bằng Đèn NeoPixel (WS2812B):**
   - **Xanh dương nhấp nháy:** Đang khởi tạo thiết bị.
   - **Đỏ:** Lỗi (Không nhận SIM, mất sóng, mất mạng GPRS/TCP).
   - **Vàng:** Không có tọa độ GPS hợp lệ (đang dò hoặc mất sóng GPS).
   - **Xanh lá:** Hoạt động hoàn hảo (Đã có GPS thật và nối mạng thành công).

5. **Trình điều khiển Module Custom (EC800Client):**
   - Không sử dụng các thư viện ngoài bị lỗi bộ đệm UART như `TinyGsmClient`. Dự án sử dụng bộ **Protocol EC800Client** được viết riêng 100% từ đầu để giao tiếp AT Command với module EC800M-CN.
   - **Tối ưu hóa Vòng lặp:** Hệ thống sẽ tự động vô hiệu hóa các lệnh kiểm tra phần cứng (SIM, Sóng, GPRS) nếu luồng TCP đã được thiết lập thành công. Chỉ khi TCP rớt mạch mới tiến hành kiểm tra lại, loại bỏ hoàn toàn các lệnh AT thừa thãi, tiết kiệm tài nguyên vi xử lý.

6. **Quản Lý Bằng FreeRTOS:**
   - Hoạt động đa luồng không giật lag. Vòng lặp giao tiếp Modem 4G (AT commands) hoàn toàn tách biệt với vòng lặp phản hồi Web Server thông qua FreeRTOS Task.

---

## 🛠 Phần Cứng & Thư Viện

- **Vi Điều Khiển:** ESP32-S3 (PlatformIO - Arduino Framework).
- **Module Mạng & GPS:** Quectel EC800 (UART: RX=18, TX=17, PWRKEY=15, RST=16).
- **Thư viện phụ trợ:**
  - `adafruit/Adafruit NeoPixel` - Điều khiển LED trạng thái RGB.
  - `WebServer` & `WiFi` (Built-in) - Phục vụ giao diện người dùng.

---

## 🚀 Hướng Dẫn Cài Đặt & Nạp Code

1. Sử dụng **VSCode + PlatformIO**.
2. Mở thư mục dự án.
3. Trong cửa sổ PlatformIO, chọn nút **Upload** hoặc gõ lệnh:
   ```bash
   pio run --target upload
   ```

---

## 📡 Theo Dõi Gỡ Lỗi (Debugging)

- Mở Serial Monitor tại tốc độ `115200` baud.
- Hệ thống hỗ trợ in toàn bộ raw data và URC nhận được từ module.
- Thời gian hiển thị ở Log Console được quy đổi về giờ Việt Nam `[TIME] dd/mm/yyyy hh:mm:ss (Vietnam Time)`.
