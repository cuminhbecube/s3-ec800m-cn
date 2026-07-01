# Trạng thái hoàn thiện project

Cập nhật: 2026-07-01.

Các lỗi nghiêm trọng trong báo cáo cũ đã được xử lý theo tag `FIX-CRIT-01` đến
`FIX-CRIT-10`. Các tính năng trước đây là placeholder cũng đã được nối vào logic thật:

- Alarm/ACC dashboard cập nhật `SystemState` và lưu NVS.
- Overspeed có thể cấu hình 10–200 km/h và được dùng để tạo alarm bit 1.
- API fingerprint add/verify/delete/clear-all thực thi qua task UART2 riêng.
- Fingerprint status trả kết quả và loại kết quả thật cho giao diện.
- FOTA `/update` đã có backend và HTTP Basic authentication.
- Buzzer phát hai beep khi boot, một beep khi GNSS chuyển sang trạng thái fix.
- Odometer được tính và lưu NVS có giới hạn tần suất ghi.

Không còn TODO/dummy/hardcode chức năng nào được biết trong source hiện tại.

## Còn phải xác nhận trên phần cứng

Đây là kiểm thử tích hợp, không phải source placeholder:

1. ACK Codec8E thực tế từ phiên bản Traccar đang dùng.
2. CEREG/QGPS/QNTP trên đúng firmware EC800 của thiết bị.
3. Chuỗi enroll/delete của đúng model cảm biến vân tay.
4. PSRAM phải hiện đủ 8 MB trong boot log.
5. Power-cycle test backlog và FOTA bằng firmware `.bin` thật.

## Log mong đợi sau release v1.2.0

Với LittleFS đang hỏng, boot đầu tiên sẽ xuất hiện:

```text
LittleFS recovery: backed up 1572864 bytes to storage2 (...)
LittleFS recovered after raw backup to storage2.
LittleFS Mounted. Backlog records: 0
```

Các boot sau phải mount trực tiếp, không backup/format lại. Nếu fingerprint chưa phản
hồi, firmware in `Retrying fingerprint sensor detection...` mỗi 10 giây; khi kết nối
được sẽ in baud rate phát hiện và `Fingerprint sensor reconnected.`

Trước khi đánh giá log này, phải nạp `v1.2.0` qua USB ít nhất một lần để cập nhật cả
bootloader và bảng phân vùng. Boot log đúng của module N16R8 phải báo flash 16 MB và
`PSRAM: OK, size=8388608 bytes`.

Chi tiết thay đổi: [FIXES_2026-07-01.md](FIXES_2026-07-01.md).
