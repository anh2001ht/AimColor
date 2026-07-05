# Coloruino

Trợ lý chiến đấu dựa trên màu sắc cho Valorant. Gồm bốn phần: ứng dụng PC đọc màn hình, Arduino giả lập chuột, loader dùng process hollowing để đóng gói ứng dụng PC thành một binary duy nhất, và công cụ tạo cấu hình dùng một lần trong quá trình cài đặt.

```
+----------------- YOUR PC (default) -----------------+
|                                                     |
|  AMDRSHelper.exe                Valorant            |
|     (cheat loader,              (game, sees the     |
|      hollows pipanel.exe         Arduino as a       |
|      into a random target)       normal USB mouse)  |
|         |                              ^            |
|         | sendto() UDP                 | HID input  |
|         v                              | via OS     |
+---------|------------------------------|------------+
          |                              |
     Ethernet to                    USB cable from
     Arduino W5500                  Arduino HID port
          |                              |
          v                              |
   +----------------------------------+  |
   | Arduino Leonardo + USB Host      |  |
   | Shield + W5500 (sandwich stack)  |--+
   +----------------------------------+
```

Mặc định mọi thứ chạy trên MỘT PC Windows. Arduino cắm vào PC đó hai đường (USB để xuất HID, Ethernet để nhận UDP). Nên build binary trên một PC RIÊNG để vệ sinh môi trường.

Tùy chọn tăng cứng 2PC với capture card: `AMDRSHelper.exe` chạy trên PC thứ hai, nhìn màn hình PC chơi game qua capture card; Arduino làm cầu nối giữa hai máy. Vanguard không thấy gì trên PC chơi game. Xem [ZZ_USER_GUIDE.md](ZZ_USER_GUIDE.md) để biết cách nối dây.

---

## Nên đọc tài liệu nào?

| Nếu bạn... | Đọc tài liệu này |
|---|---|
| Mới làm quen dự án | [ZZ_USER_GUIDE.md](ZZ_USER_GUIDE.md) - phần giới thiệu dễ hiểu |
| Build từ source | [ZZ_BUILD_GUIDE.md](ZZ_BUILD_GUIDE.md) |
| Muốn hiểu bên trong hoạt động ra sao | [ZZ_ARCHITECTURE.md](ZZ_ARCHITECTURE.md) |
| Lo về khả năng bị phát hiện | [ZZ_SECURITY.md](ZZ_SECURITY.md) |
| Làm trong một component | file `ZZ_README.md` trong thư mục của component đó |

---

## Bốn binary nhìn nhanh

```
coloruino-loader/    ->  AMDRSHelper.exe
                         Binary duy nhất từng nằm trên PC chơi.
                         Hỏi license một lần, cache vào auth.dat,
                         giải mã rồi hollow pipanel.exe vào một
                         process đích ngẫu nhiên.

coloruino-app/       ->  pipanel.exe
                         Cheat thực tế. Nằm trong AMDRSHelper.exe
                         dưới dạng byte đã mã hóa, được giải mã
                         trong RAM khi chạy, không bao giờ ghi
                         plaintext xuống đĩa PC chơi.
                         Host WebUI trên :13548 để tuning trực tiếp.

coloruino-fw/        ->  coloruino-fw.ino  (Arduino)
                         Nhận UDP có hình dạng DNS, thực thi bộ lệnh
                         M/P/F/L dưới dạng report chuột USB HID.
                         Chuyển tiếp nút bấm của chuột thật qua
                         USB Host Shield.

coloruino-config-    ->  config_generator.exe
generator/               CLI dùng một lần để ghi file `data` đã mã hóa.
                         Trong setup hiện tại loader đã gộp phần này
                         (data_writer.cpp), còn generator chỉ để debug
                         phía supplier.
```

---

## Phần cứng cần có

- Một Arduino Leonardo (hoặc board tương thích 32U4).
- Một USB Host Shield 2.0.
- Một W5500 Ethernet shield.
- 6 dây jumper cho kiểu sandwich SPI (đường SPI của W5500 đi vòng qua USB Host Shield tới header ICSP của Arduino).
- Hai cáp USB.
- Một cáp Ethernet.

Đi dây là phần khó nhất. Ảnh chi tiết và các bước lắp ráp nằm trong thread UC được dẫn từ [ZZ_USER_GUIDE.md](ZZ_USER_GUIDE.md).

---

## Khởi động nhanh (mặc định 1PC, secrets đã rotate và binary đã build)

Trên PC chơi:

1. Copy `AMDRSHelper.exe` vào một nơi nào đó. `C:\Program Files\AMD\CNext\CNext\` khá hợp lý để hòa vào cài đặt AMD thật.
2. Cắm Arduino vào PC: cáp USB vào một cổng trống (xuất HID mouse), cáp Ethernet vào NIC phụ (nhận UDP).
3. Đặt IP tĩnh trên NIC phụ đó, cùng subnet với firmware.
4. Double-click `AMDRSHelper.exe`. Nhập license một lần khi được hỏi. Chuột giật nhẹ = đang sống.
5. Các lần chạy sau không hỏi nữa (`auth.dat` nằm cạnh exe).

Để tuning từ điện thoại: mở `http://<play-pc-ip>:13548/` trong trình duyệt, nhập thông tin Basic-auth WebUI bạn đã rotate.

---

## Bố cục repository

```
coloruino/
+--- README.md             (bạn đang ở đây)
+--- USER_GUIDE.md         hướng dẫn dễ hiểu
+--- BUILD_GUIDE.md        build/sign/deploy đầy đủ
+--- ARCHITECTURE.md       kỹ thuật toàn hệ thống
+--- SECURITY.md           threat model
+--- rotate_secrets.py     rotate license / keys / WebUI creds
+--- post.md               bài release UC
|
+--- coloruino-app/        ứng dụng C++ Win32 (pipanel.exe)
+--- coloruino-loader/     loader process-hollowing C++ (AMDRSHelper.exe)
+--- coloruino-fw/         firmware Arduino Leonardo
```

---

## Ghi chú

- Các thuật ngữ "silent aim" (`mode_a`), "flicker" (`nonmode_a`), "aimbot" (`apply_delta`) là các mode cụ thể trong ứng dụng này. Xem glossary trong [ZZ_ARCHITECTURE.md](ZZ_ARCHITECTURE.md).
- Firmware Arduino dựa vào profile Leonardo ĐÃ PATCH (USB VID/PID đổi để bắt chước một chuột gaming thật). Xem [coloruino-fw/ZZ_README.md](coloruino-fw/ZZ_README.md) để biết patch.
- WebUI cố ý mang thương hiệu "Spotify Web Player" trong HTML, còn metadata binary là "AMD Radeon Software Helper". Tách danh tính là có chủ đích, xem [ZZ_SECURITY.md](ZZ_SECURITY.md).
- Constants bị nhân đôi giữa app và loader (HWID salt, license hash key, config XOR key) vì loader phải tạo byte `LICENSE_HWID` giống hệt app tính lúc startup. Nếu rotate một bên, cập nhật CẢ HAI. `rotate_secrets.py` sẽ nhắc việc này.
- Đừng chỉ rotate license riêng lẻ - license + AES build salt + file `data` theo máy có liên kết với nhau. Dùng `rotate_secrets.py` từ đầu tới cuối.
- **Silent aim và flickbot là các mode RỦI RO CAO.** Xem [ZZ_SECURITY.md](ZZ_SECURITY.md) để biết lý do. Dùng hạn chế hoặc không dùng.

---

## Giấy phép / sử dụng

Dành cho nghiên cứu cá nhân và tham khảo giáo dục. Không phân phối lại dưới dạng dịch vụ trả phí. Không dùng với người không đồng ý. [ZZ_SECURITY.md](ZZ_SECURITY.md) ghi rõ thứ này che giấu và không che giấu gì khỏi Vanguard.
