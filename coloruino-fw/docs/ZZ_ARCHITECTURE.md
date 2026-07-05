# Kiến trúc Firmware Arduino

## Tổng quan hệ thống

```
 Arduino Board
+-----------------------------------------------------+
| |
| USB Host Port USB Device Port |
| +----------+ +--------------+ |
| | Real | HIDUniversal | PluggableUSB | |
| | Mouse |--> MouseRpt --> | |--> To PC
| | | Parser | Interface 0 | |
| +----------+ | (Mouse) | |
| | | |
| Ethernet (W5500) | Interface 1 | |
| +----------+ | (Kbd/Con/Vnd)| |
| | UDP | parse() | (stub) | |
| | Server |--> exec() ----> | | |
| | :5353 | +--------------+ |
| +----------+ |
| |
+-----------------------------------------------------+
```

## Luồng dữ liệu

### Passthrough chuột thật

```
Real Mouse USB Report (7+ bytes)
 |
 v
USB Host Shield (SPI)
 |
 v
HIDUniversal -> MouseRptParser::Parse()
 |
 +-- Filter: Report ID must be 1, length >= 7
 +-- Extract: buttons(1B), dx(2B LE), dy(2B LE), wheel(1B)
 +-- Deduplicate: skip if nothing changed
 |
 v
Mouse.report(buttons, dx, dy, wheel)
 |
 v
USB HID Report to PC (single packet)
```

### Command Injection

```
UDP Packet from Coloruino PC
 |
 v
Udp.parsePacket() + Udp.read()
 |
 v
parse(buffer)
 |
 +-- Split on ';' (semicolons)
 +-- For each segment:
 |
 v
exec(cmd)
 |
 +-- Extract: type (first char), x, y (parseInt)
 +-- Read: real = mouseParser.prevButtons
 |
 +-- 'M' -> Mouse.report(real, x, y)
 +-- 'L' -> Mouse.report(real|LEFT, 0, 0)
 | Mouse.report(real, 0, 0)
 +-- 'P' -> Mouse.report(real|LEFT, x, y)
 | Mouse.report(real, -x, -y)
 +-- 'F' -> Mouse.report(real|LEFT, x, y)
 Mouse.report(real, 0, 0)
```

## Kiến trúc USB Descriptor

### Vì sao có hai Interface

Chuột gaming thật (VID 0x????, PID 0x????) có hai HID interface. Công cụ fingerprint USB device có thể phân biệt thiết bị dựa trên bố cục descriptor. Một thiết bị single-interface tự nhận là chuột này sẽ dễ bị detect.

### Interface 0: Mouse (ImprovedMouse)

Đăng ký trước qua PluggableUSB -> nhận Endpoint 1.

**HID Report Descriptor:**
```
Usage Page: Generic Desktop
Usage: Mouse
 Collection: Application
 Report ID: 1
 Usage: Pointer
 Collection: Physical
 5 Buttons (Left, Right, Middle, Back, Forward)
 16-bit X axis (-32767 to 32767)
 16-bit Y axis (-32767 to 32767)
 8-bit Wheel (-127 to 127)
```

**Report (6 byte, Report ID 1):**
```
Byte 0: Buttons bitmap (5 bits used, 3 padding)
Byte 1-2: X axis (int16_t, little-endian)
Byte 3-4: Y axis (int16_t, little-endian)
Byte 5: Wheel (int8_t)
```

### Interface 1: Keyboard/Consumer/Vendor (SecondHIDIface)

Đăng ký sau -> nhận Endpoint 2. Stub descriptor thuần - không bao giờ gửi data.

**Report Descriptor (tổng 140 byte):**

| Report ID | Collection | Size | Mục đích |
|-----------|------------|------|----------|
| 3 | Keyboard | 7 byte (1 modifier + 6 key) | Emulate keyboard |
| 2 | Consumer Control | 2 byte (16-bit usage) | Media key |
| 6 | Vendor 0xFF00 | 2 byte | Vendor-specific data |
| 7 | Vendor 0xFF01 | 7 byte (Feature report) | Vendor config |
| 8 | System Control | 1 byte (3 bit + padding) | Power/Sleep/Wake |

**PluggableUSB Registration:**
```
SecondHIDIface_ constructor:
 1. _epType[0] = EP_TYPE_INTERRUPT_IN
 2. PluggableUSB().plug(this) // registers before USB attach()

getInterface():
 Returns 25-byte descriptor block:
 - InterfaceDescriptor (9B): HID class, keyboard protocol, 1 endpoint
 - HIDDescriptor (9B): HID 1.10, report descriptor length = 140
 - EndpointDescriptor (7B): IN interrupt, 8B max packet, 1ms interval

getDescriptor():
 Responds to GET_DESCRIPTOR(Report, our interface):
 Returns 140-byte _sHIDReportDesc from PROGMEM

setup():
 ACKs any HID class request (GET_REPORT, SET_REPORT, etc.)
 Returns true for our interface, false otherwise
```

## Giữ trạng thái nút

### Vấn đề

Nếu không track button state, các lệnh inject sẽ:
1. Gửi `report(0, dx, dy)` - OS thấy mọi nút được thả
2. Chuột thật gửi `report(buttons, 0, 0)` - OS thấy nút được bấm lại
3. Việc này tạo edge press/release giả làm hỏng:
 - Drag operation
 - Feature dựa trên latch (mode_a, nonmode_a) phía PC
 - Bất kỳ hành vi hold-to-activate nào

### Giải pháp

```cpp
// In exec():
uint8_t real = mouseParser.prevButtons; // current real button state

// All injected commands OR with real buttons:
Mouse.report(real | MOUSE_LEFT, x, y); // click preserves existing buttons
Mouse.report(real, -x, -y); // snapback restores real state
```

`prevButtons` được update bởi `MouseRptParser::Parse()` trên mỗi report chuột thật,
luôn phản ánh trạng thái nút vật lý hiện tại.

## report() so với move()/press()/release()

### Arduino Mouse Library chuẩn

```
Mouse.press(MOUSE_LEFT); // Report 1: buttons=LEFT, x=0, y=0
Mouse.move(100, 50); // Report 2: buttons=LEFT, x=100, y=50
Mouse.release(MOUSE_LEFT); // Report 3: buttons=0, x=0, y=0
```
3 USB report cho một hành động. State trung gian thấy được với OS.

### ImprovedMouse report()

```
Mouse.report(buttons, x, y, wheel); // Report 1: everything in one packet
```
1 USB report. Atomic. Không state trung gian.

Điều này rất quan trọng cho lệnh P (silent aim):
```
// Two reports, back-to-back:
Mouse.report(real | LEFT, x, y); // Move + click in one report
Mouse.report(real, -x, -y); // Snap back + release in one report
```
OS thấy một click 1-frame ở vị trí offset. Không có movement thấy được.

## Timing

### Main Loop

```cpp
void loop() {
 // UDP checked FIRST for lowest command latency (saves 100-500us vs old order)
 packetSize = Udp.parsePacket();
 if (packetSize) {
 Udp.read(...);
 parse(buffer); // Execute all commands in packet
 }
 
 Usb.Task(); // Poll USB Host for real mouse events (~125us)
}
```

Không delay trong loop. Lệnh UDP xử lý trước USB Host polling để giảm command latency.

### Lệnh P - Atomic vs Split Mode

```
P_COOLDOWN = 200ms (configurable via 'K' command)
P_SPLIT_DELAY = 1250us (configurable via 'D' command)

exec('P'):
 if (millis() - lastPTime < P_COOLDOWN) return;
 
 if (P_SPLIT_DELAY > 0): // Split mode
 report(real, x, y) // move only
 delayMicroseconds(P_SPLIT_DELAY)
 report(real | LEFT, 0, 0) // click at destination
 report(real, -x, -y) // snapback + release
 else: // Atomic mode
 report(real | LEFT, x, y) // move+click
 report(real, -x, -y) // snapback+release
 
 lastPTime = millis();
```

Split mode tách movement khỏi click qua các USB frame, khớp timing firmware cũ
khi `optimizedMove()` hoàn tất trước `Mouse.click()`.

### Configuration Commands

| Command | Variable | Description |
|---------|----------|-------------|
| `D<value>\r` | `P_SPLIT_DELAY` | Đặt split delay theo microsecond (0=atomic) |
| `K<value>\r` | `P_COOLDOWN` | Đặt cooldown theo millisecond |

Cả hai được PC gửi lúc startup và khi đổi qua web UI.

## Sinh địa chỉ MAC

```cpp
void genMAC() {
 randomSeed(analogRead(0) + micros()); // entropy from analog noise + timer
 mac[0] = 0xEE; // locally administered, unicast
 for (i = 1..5)
 mac[i] = random(256);
}
```

Byte đầu `0xEE`:
- Bit 0 = 0: unicast (không multicast)
- Bit 1 = 1: locally administered (không globally unique)

MAC khác nhau mỗi boot ngăn tracking thiết bị ở tầng mạng.

## Integer Parser

`parseInt()` custom tránh overhead `atoi`/`strtol`:

```cpp
int parseInt(char** ptr) {
 int value = 0;
 bool neg = (**ptr == '-');
 if (neg) (*ptr)++;
 
 while (**ptr >= '0' && **ptr <= '9') {
 value = (value << 3) + (value << 1) + (**ptr - '0');
 // value*8 + value*2 + digit
 // = value*10 + digit
 (*ptr)++;
 }
 
 return neg ? -value : value;
}
```

Nhân bằng bit-shift: `(x << 3) + (x << 1)` = `x * 8 + x * 2` = `x * 10`.
Nhanh hơn phép nhân thật trên AVR.
