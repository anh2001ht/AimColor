# No-Arduino AutoClicker

Project nhỏ này chạy trực tiếp trên Windows và tự click bằng API `SendInput`.
Không cần Arduino, USB Host Shield, W5500, firmware, driver, Python, hay dependency ngoài.

Mục đích: desktop automation đơn giản, test UI, hoặc tác vụ cá nhân hợp pháp. Không có phần ẩn process, anti-debug, spoof phần cứng, hay tích hợp game/anti-cheat.

## Chạy nhanh

Double-click:

```text
start_autoclicker.bat
```

Test tool:

```text
start_all.bat
```

`start_all.bat` opens both the Python auto-clicker and a local color click tester.
The tester target uses this exact color:

```text
#EB69FE / RGB(235, 105, 254)
```

Move your cursor onto the color square, press `F8`, and watch the
`Clicks on target` counter increase.

Local pull test:

| Key | Action |
|---|---|
| F6 | Pull cursor once to the center of the tester color square |
| F7 | Toggle local auto-pull to the tester color square |
| F8 | Toggle auto-clicker |

Hotkey:

| Phím | Chức năng |
|---|---|
| F8 | Bật/tắt auto click |
| F9 | Click một lần |
| F10 | Thoát |

Mặc định tool click chuột trái mỗi 100 ms, giữ nút trong 20 ms.

## Tùy chỉnh

Mở terminal trong thư mục này rồi chạy:

```powershell
.\autoclicker.ps1 -IntervalMs 250 -HoldMs 30
```

Click chuột phải:

```powershell
.\autoclicker.ps1 -Button right
```

Thêm jitter delay ngẫu nhiên:

```powershell
.\autoclicker.ps1 -IntervalMs 100 -JitterMs 50
```

Bắt đầu ở trạng thái đang chạy thay vì paused:

```powershell
.\autoclicker.ps1 -StartActive
```

## Ghi chú

- Cursor đang ở đâu thì tool click ở đó.
- F8/F9/F10 được đọc bằng trạng thái phím global. Nếu app đang focus cũng dùng các phím này, app đó vẫn có thể nhận phím.
- Nếu Windows hỏi permission vì app đích chạy quyền admin, hãy chạy terminal hoặc file `.bat` bằng quyền tương ứng.
- `autoclicker.py` vẫn có sẵn như bản phụ nếu máy bạn có Python, nhưng launcher mặc định dùng PowerShell để không cần cài thêm gì.
