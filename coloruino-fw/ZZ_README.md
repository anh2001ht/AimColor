# coloruino-fw - Firmware Arduino HID

Firmware Arduino để inject lệnh chuột USB HID kèm passthrough chuột thật.
Nhận lệnh UDP từ ứng dụng PC Coloruino và inject chúng như report USB HID hợp lệ,
đồng thời giữ nguyên trạng thái nút của chuột thật.

> **Xem thêm**:
> [README cấp root](../ZZ_README.md), [USER_GUIDE](../ZZ_USER_GUIDE.md),
> [BUILD_GUIDE](../ZZ_BUILD_GUIDE.md), [ARCHITECTURE](../ZZ_ARCHITECTURE.md),
> [SECURITY](../ZZ_SECURITY.md).

> **Trạng thái Phase 6.1** (sau pass tối ưu responsiveness mạnh):
> - USB descriptor mimic chuột gaming thật bạn mimic (VID 0x???? / PID 0x????).
> - MAC OUI rotate mỗi boot từ pool 6 vendor (Intel/Realtek/Apple/Samsung/Microsoft/Dell).
> - Sub-stepping lệnh M:
> - Tối đa 5 sub-step (trước là 8) - flick lớn hoàn tất nhanh hơn khoảng 3 ms.
> - Ngưỡng sub-step tăng lên <10 px (trước <3 px) - hầu hết micro-correction
> tracking bắn thành một report đơn, latency thấp hơn.
> - Jitter giữa sub-step: uniform 100-300 us (trước bimodal 150-405 us
> + 6% x 1000-1255 us). Bỏ slow mode bimodal - nó tạo cảm giác ì hơn
> mà không đem lại nhiều lợi ích detection ở cadence host-observed hiện có.
> - Handler P (silent aim) và F (flicker) KHÔNG ĐỔI - không humanization.
> - Hold time của L (click): uniform 20-67 ms.
> - Network protocol: UDP dạng DNS trên port 5353 với payload base32 XOR-encrypted + CRC-8.
> - Main loop: UDP được poll cả TRƯỚC và SAU `Usb.Task()` - bắt packet đến trong
> cửa sổ blocking 100-500 us của `Usb.Task`.
> - Ép thứ tự HID interface (Mouse trước, Keyboard sau - khớp thứ tự descriptor thiết bị thật).
> - Compile flags: `-O2` (đặt qua `build.extra_flags` trong `boards.txt` đã patch).
> Ghi đè default `-Os` của Arduino.
>
> **Trade-off với heuristic `extra/mouse-polling-monitor`**: bỏ slow mode bimodal
> làm giảm std dev host-observed. Trên test rig sạch không có traffic USB khác,
> verdict polling-monitor có thể rơi từ NATURAL xuống SUSPICIOUS. Chạy monitor
> với setup live sau khi flash. Nếu vẫn NATURAL thì ổn. Nếu đổi trạng thái,
> revert block jitter trong `sub_step_move()` về bản bimodal trước đó.

---

## Setup phần cứng

Layout 1PC mặc định - USB output của Arduino và Ethernet input W5500
đều nối vào CÙNG PC chạy Valorant và `AMDRSHelper.exe`.

```
   Real USB mouse (the human's actual mouse)
        |
        | plugged into the USB Host Shield
        v
   +--------------------------------+
   | Arduino Leonardo               |
   | + USB Host Shield (passthrough)|
   | + W5500 Ethernet Shield        |
   +--------------------------------+
     |                       ^
     | USB cable             | Ethernet
     | (HID mouse OUT)       | (UDP M/P/F/L cmds IN)
     v                       |
   +--------------------------------+
   | YOUR PC                        |
   | - Valorant sees mouse via OS   |
   | - AMDRSHelper.exe sends UDP    |
   +--------------------------------+
```

### Components

- Arduino board (kiến trúc AVR, ví dụ Leonardo / Pro Micro - phải có native USB).
- USB Host Shield 2.0 (để passthrough chuột thật).
- W5500 Ethernet shield.
- Chuột USB thật nối vào cổng USB Host Shield (KHÔNG nối trực tiếp vào PC).
- 6 dây jumper cho sandwich SPI W5500 (xem `ZZ_USER_GUIDE.md` cấp root).

### Network Configuration

Firmware ship với placeholder network default. Sửa trước khi flash.

| Parameter | Placeholder default | Ghi chú |
|-----------|---------------------|---------|
| IP        | 192.168.1.216       | IP tĩnh của Arduino. NIC phụ của PC phải cùng /24. |
| DNS       | 192.168.1.1         | Chỉ cosmetic; Arduino không lookup DNS. |
| Gateway   | 192.168.1.1         | Cosmetic vì cùng lý do. |
| Subnet    | 255.255.255.0       | /24, ổn trong gần như mọi setup home. |
| UDP Port  | 5353                | Port mDNS; ứng dụng cheat gửi packet dạng DNS tới đây. |
| MAC       | Random mỗi boot (rotate từ 6 OUI vendor consumer thật) hoặc cố định qua `MAC_RANDOM false`. | |

---

## USB Device Identity

Arduino tự trình diện với OS như một chuột gaming thật. Firmware release ship
với identity placeholder. Bạn PHẢI patch chúng để khớp một sản phẩm chuột gaming
thật trước khi flash:

| Field | Value (PLACEHOLDER, thay thế) |
|-------|--------|
| VID | 0x???? |
| PID | 0x???? |
| Manufacturer | "PLACEHOLDER_MFR" |
| Product | "PLACEHOLDER_PRODUCT" |

Đặt qua define trong `boards.txt` (`-DUSB_VID`, `-DUSB_PID`, v.v.) và fallback `#define`.

Capture device descriptor của CHÍNH chuột gaming vật lý của bạn bằng
Wireshark/USBPcap hoặc Windows USB device viewer, rồi patch local Arduino AVR core
`USBCore.cpp` + `USBDesc.h` để khớp từng byte (class, subclass, protocol,
packetSize0, v.v.).

---

## Kiến trúc Dual HID Interface

Chuột thật mà firmware clone có hai HID interface. Cả hai được replicate:

### Interface 0 - Mouse (ImprovedMouse.h)

Primary mouse interface với hỗ trợ boot protocol.

**HID Report (6 byte, Report ID 1):**
| Byte | Field | Type |
|------|-------|------|
| 0 | Buttons (5 buttons) | uint8_t bitmap |
| 1-2 | X axis | int16_t |
| 3-4 | Y axis | int16_t |
| 5 | Wheel | int8_t |

**Method chính:** `Mouse.report(buttons, x, y, wheel)` gửi một report HID hoàn chỉnh
trong một USB packet. Khác với method Arduino Mouse chuẩn (`move`, `press`,
`release`), cách này tránh tạo report trung gian không mong muốn.

### Interface 1 - Keyboard/Consumer/Vendor Stub (SecondHIDIface.h)

Stub chỉ có descriptor. Không bao giờ gửi data. Host poll EP2 IN và nhận NAK,
đây là hành vi bình thường.

**Report descriptor (tổng 140 byte):**
| Report ID | Usage | Mục đích |
|-----------|-------|----------|
| 3 | Keyboard | 8 modifier key + 6 key code |
| 2 | Consumer Control | Media key (16-bit usage) |
| 6 | Vendor 0xFF00 | 2 byte vendor data |
| 7 | Vendor 0xFF01 (Feature) | Feature report 7 byte |
| 8 | System Control | Power/Sleep/Wake (3 bit) |

Interface thứ hai tồn tại chỉ để khớp fingerprint USB descriptor của chuột thật.
Nếu không có nó, thiết bị chỉ có một interface, có thể phân biệt được với phần cứng thật.

---

## Passthrough chuột thật

### hidcustom.h - MouseRptParser

Parse report USB Host inbound từ chuột thật và forward qua device interface của Arduino.

**Report format được parse:** `[ReportID=1] [Buttons] [Xlo] [Xhi] [Ylo] [Yhi] [Wheel]`

**Tối ưu so với v1:**
- Một call `Mouse.report()` duy nhất kết hợp buttons + movement + wheel
- Chỉ gửi report nếu có thứ thật sự thay đổi
- Wheel không còn bị drop khi dx==0 && dy==0
- Không có report đổi nút riêng trước report movement

`prevButtons` public để command executor đọc trạng thái nút thật.

---

## Command Protocol

Lệnh đến dưới dạng UDP packet. Nhiều lệnh có thể phân tách bằng dấu chấm phẩy trong một packet.

### Format
```
<prefix><x>,<y>\r
```

### Commands

| Prefix | Name | Hành vi | HID Reports Sent |
|--------|------|---------|------------------|
| `M` | Move | Di chuyển chuột, giữ nút thật | 1: `report(real, x, y)` |
| `L` | Click | Left click (press + release) | 2: `report(real|LEFT, 0, 0)` rồi `report(real, 0, 0)` |
| `P` | Silent Aim | Move+click, rồi snapback+release | Atomic (split=0): 2 report. Split (split>0): move -> delay -> click -> snapback (3-4 report) |
| `F` | Flicker | Move+click, rồi release (giữ vị trí) | 2: `report(real|LEFT, x, y)` rồi `report(real, 0, 0)` |
| `D` | Split Delay | Đặt P_SPLIT_DELAY | Cấu hình delay us giữa move và click trong split P mode |
| `K` | Cooldown | Đặt P_COOLDOWN | Cấu hình cooldown ms giữa các lệnh P |

### Giữ trạng thái nút

Mỗi lệnh inject đọc `mouseParser.prevButtons` để lấy trạng thái nút hiện tại của chuột thật. Điều này ngăn:
- Edge release/press giả khi inject movement
- Phá các feature dựa trên latch (mode_a, nonmode_a) ở phía PC
- OS thấy trạng thái nút ảo

### Ví dụ
```
M100,50 -> move 100 right, 50 down
L -> left click
P200,100 -> silent aim: move (200,100), click, snap back (-200,-100), release
F200,100 -> flicker: move (200,100), click, release
M10,0;L;M-10,0 -> chained: move, click, move back
```

---

## Địa chỉ MAC

Hai mode điều khiển bởi `#define MAC_RANDOM`:

| Mode | Hành vi |
|------|---------|
| `true` (default) | Tạo MAC random mỗi boot bằng `analogRead(0) + micros()` làm seed. Byte đầu luôn `0xEE`. |
| `false` | Dùng array hardcoded `FIXED_MAC` |

MAC random ngăn fingerprint thiết bị ở tầng mạng qua các lần reboot.

---

## Integer Parser

`parseInt()` custom tránh overhead stdlib. Dùng nhân bằng bit-shift:
`value = (value << 3) + (value << 1) + digit`, tương đương `value * 10 + digit`.

Xử lý số âm bằng dấu `-` đầu chuỗi.

---

## P Command Cooldown & Split Mode

Silent aim (`P`) có cooldown cấu hình được (`P_COOLDOWN`, default 200ms) để tránh rapid-fire.
Cấu hình qua lệnh `K` từ PC. Tách biệt với debounce 100ms phía PC.

### Split P Mode

Khi `P_SPLIT_DELAY > 0` (cấu hình qua lệnh `D`), lệnh P tách thành các USB report riêng:

1. `report(real, x, y)` - chỉ movement (không click)
2. `delayMicroseconds(P_SPLIT_DELAY)` - chờ giữa các USB frame
3. `report(real | LEFT, 0, 0)` - click tại vị trí mới
4. `report(real, -x, -y)` - snapback + release

Khớp hành vi firmware cũ nơi `optimizedMove()` hoàn tất trước khi `Mouse.click()` chạy.
Default: 1250 us (nằm giữa hai interval USB polling 1kHz).

Khi `P_SPLIT_DELAY == 0`, atomic mode: move+click trong một report, rồi snapback
(tổng 2 report).

---

## Build

### Requirements
- Arduino IDE hoặc PlatformIO
- Libraries: SPI, Ethernet, USBHost (USB Host Shield 2.0)
- Board có native USB (Leonardo, Pro Micro, v.v.)

### Board Configuration (boards.txt)
```
<board>.build.vid=0x????   # PLACEHOLDER: real gaming-mouse VID
<board>.build.pid=0x????   # PLACEHOLDER: real gaming-mouse PID
<board>.build.usb_manufacturer="PLACEHOLDER_MFR"
<board>.build.usb_product="PLACEHOLDER PRODUCT"
```

### File Structure
```
coloruino-fw/
+-- coloruino-fw.ino # Main sketch (setup, loop, command parsing)
+-- ImprovedMouse.h # Custom Mouse_ class with report() method
+-- SecondHIDIface.h # Interface 1 descriptor stub
+-- hidcustom.h # USB Host mouse report parser
```

### Installation Notes
- `ImprovedMouse.h` thay thế Arduino Mouse library chuẩn. `Mouse_.cpp` tương ứng cũng phải có (cung cấp HID report descriptor và implementation `report()`).
- `SecondHIDIface.h` phải được instantiate đúng một lần ở global scope trước khi `USBDevice.attach()` chạy.
- Thứ tự đăng ký quan trọng: ImprovedMouse đăng ký trước (Interface 0 / EP1), rồi SecondHIDIface (Interface 1 / EP2).
