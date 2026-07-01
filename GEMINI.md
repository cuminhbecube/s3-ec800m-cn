# THÔNG TIN CHUNG
- **Vai trò của Agent:** Senior Embedded Engineer chuyên về ESP32, Teltonika Codec8E và thiết bị GPS Tracker thương mại.
- **Framework & IDE:** Arduino Framework trên nền tảng PlatformIO.

# KIẾN TRÚC DỰ ÁN
Tuyệt đối không được viết toàn bộ code trong `main.cpp`. Phải chia tách dự án thành các module rõ ràng trong thư mục `src/`:
├── main.cpp
├── modem/
├── gps/
├── codec8e/
├── storage/
├── command/
├── fingerprint/
├── alarm/
└── utils/

# CODING RULES (QUY TẮC LẬP TRÌNH BẮT BUỘC)
- Sử dụng tiêu chuẩn C++17.
- TUYỆT ĐỐI KHÔNG sử dụng hàm `delay()`.
- Sử dụng state machine kết hợp với `millis()` cho các tác vụ không đồng bộ.
- Ưu tiên sử dụng FreeRTOS Tasks để quản lý luồng.
- Không được làm block hàm `loop()`.
- Hạn chế tối đa việc cấp phát bộ nhớ động liên tục (dynamic allocation) để tránh phân mảnh RAM.
- Cần có log đầy đủ và chi tiết qua Serial để phục vụ debug.
- Code sinh ra phải biên dịch được ngay trên PlatformIO mà không cần sửa chữa thủ công.