# Báo cáo hoàn thiện tính năng — 2026-07-01

## COMPLETE-01 — Alarm và ACC

- Bỏ API success giả.
- Hỗ trợ ACC vật lý hoặc ACC mô phỏng, chuyển mode ngay khi thao tác dashboard.
- SOS, fatigue, GPS antenna, power cut và collision có state thật, NVS và alarm bit.
- Mapping alarm hex: SOS bit 0, overspeed bit 1, fatigue bit 2, GPS antenna bit 5,
  power cut bit 8, collision bit 20.

## COMPLETE-02 — Overspeed

- Cấu hình hợp lệ 10–200 km/h.
- Lưu NVS, trả qua API config/status và dùng trực tiếp để sinh overspeed alarm.

## COMPLETE-03 — Fingerprint UX và scheduling

- Status API trả sensor state, count, last result và result type thật.
- Add/verify/delete/clear-all cập nhật thông báo thành công hoặc lỗi.
- Tách `Task_Fingerprint` khỏi `Task_Sensors`; enroll dài không còn làm trễ ACC,
  VBAT hoặc lịch ghi backlog.

## COMPLETE-04 — Indicator events

- Hai beep ngắn khi boot theo REQUIREMENTS.
- Một beep khi GNSS chuyển từ no-fix sang fix, có cooldown 60 giây chống kêu liên tục
  khi tín hiệu chập chờn.
- Ba beep cho fingerprint match thành công giữ nguyên.

## COMPLETE-05 — Odometer bền vững

- Mileage được load từ NVS khi boot.
- Chỉ ghi lại khi có thay đổi và đạt 1 km hoặc 10 phút, tránh ghi flash mỗi GPS poll.
- Factory reset đưa mileage và toàn bộ alarm/config về default.

## COMPLETE-06 — Firmware metadata

- Firmware version tăng từ `1.0.0` lên `1.1.0`.
- Loại bỏ server constants placeholder trùng với runtime defaults.

## Kiểm chứng

- Source scan: không còn marker chức năng `TODO`, `FIXME`, `dummy`, `hardcoded` hoặc `ignored for now`.
- PlatformIO environment: `esp32-s3-devkitc1-n16r8`.
- Build PASS: RAM 46.836 byte (14,3%), firmware 957.673 byte (28,7% OTA slot).
- Kiểm thử phần cứng còn lại được liệt kê trong `outstanding.md`.

## Hotfix runtime 1.1.1

- LittleFS lỗi `Corrupted dir pair` được sao lưu raw sang partition `storage2`
  trước khi format phục hồi. Header backup được ghi cuối cùng kèm checksum.
- Partition mới hoàn toàn vẫn được format trực tiếp sau khi xác nhận toàn `0xFF`.
- Fingerprint khởi động tối đa 3 giây và tự dò lại mỗi 10 giây nếu sensor lên nguồn trễ.
- Buzzer dùng `tone(pin, frequency)` và tự gọi `noTone()`, không còn double-stop
  LEDC do `tone(..., duration)` tự detach trước.

## Hotfix runtime 1.1.2

- Dùng `esp_littlefs_format_partition()` trực tiếp trên partition `spiffs`; tránh
  lỗi label/context của `LittleFS.format()` sau một lần VFS mount thất bại.
- Bỏ hoàn toàn Arduino `tone()/noTone()`. Buzzer giữ LEDC channel 7 được attach
  suốt vòng đời và chỉ đổi frequency về 0 khi dừng.
- Khởi tạo `freeHeap` ngay trong setup để alive log đầu tiên không còn báo 0.

## Hotfix runtime 1.1.3

- Sau khi raw backup thành công, erase trực tiếp toàn partition `spiffs`, rồi mount
  với `formatOnFail=true` và label tường minh. Không còn phụ thuộc formatter phải
  đọc được metadata LittleFS đang hỏng.
- Boot log chỉ báo PSRAM `USABLE` khi SPIRAM heap thực sự có dung lượng; không dùng
  `psramFound()` vì core hiện tại có thể trả true dù heap size bằng 0.

## Hotfix runtime 1.1.4

- Unregister context `spiffs` còn sót sau mount lỗi trước khi recovery.
- Xác minh 8 KB đầu partition đều `0xFF` sau erase.
- Format bằng `esp_littlefs_format_partition()` trong trạng thái context sạch,
  sau đó mount với `formatOnFail=false`; không còn gọi formatter hai lần.

## Hotfix runtime 1.1.5

- Sửa partition subtype từ `spiffs` sang `littlefs`; đây là subtype formatter
  `esp_littlefs` yêu cầu trên framework hiện tại.
- Đổi label thành `littlefs`; giữ nguyên offset `0x670000` và size `0x180000`, nên
  raw backup trong `storage2` và layout flash không bị dịch chuyển.
- Recovery tìm partition theo label với subtype `ANY` để nhận cả layout legacy.
- Bắt buộc upload qua cáp/PlatformIO để ghi partition table; FOTA chỉ ghi app và
  không thể áp dụng thay đổi subtype này.

## Release v1.2.0 — Toolchain mới nhất

- Nâng lên pioarduino `55.03.39` / Arduino-ESP32 `3.3.9` / ESP-IDF `5.5.4`.
- Chuyển LittleFS và LEDC sang API native Arduino-ESP32 3.x.
- Giữ nguyên layout flash N16, thêm cấu hình QIO + OPI đúng module N16R8.
- Build release PASS: RAM 14,7%, flash OTA slot 35,2%.
- Lần nâng cấp này phải nạp bằng USB để `partitions.bin` mới được ghi; không dùng
  FOTA cho lần chuyển từ 1.1.x lên 1.2.0.
