# Arduino Firmware Architecture

## System Overview

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

## Data Flow

### Real Mouse Passthrough

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

## USB Descriptor Architecture

### Why Two Interfaces

The real a real gaming mouse (VID 0x????, PID 0x????) has two HID interfaces. USB device fingerprinting tools can distinguish devices by their descriptor layout. A single-interface device claiming to be this mouse would be detectable.

### Interface 0: Mouse (ImprovedMouse)

Registered first via PluggableUSB -> gets Endpoint 1.

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

**Report (6 bytes, Report ID 1):**
```
Byte 0: Buttons bitmap (5 bits used, 3 padding)
Byte 1-2: X axis (int16_t, little-endian)
Byte 3-4: Y axis (int16_t, little-endian)
Byte 5: Wheel (int8_t)
```

### Interface 1: Keyboard/Consumer/Vendor (SecondHIDIface)

Registered second -> gets Endpoint 2. Pure descriptor stub - never sends data.

**Report Descriptors (140 bytes total):**

| Report ID | Collection | Size | Purpose |
|-----------|------------|------|---------|
| 3 | Keyboard | 7 bytes (1 modifier + 6 keys) | Keyboard emulation |
| 2 | Consumer Control | 2 bytes (16-bit usage) | Media keys |
| 6 | Vendor 0xFF00 | 2 bytes | Vendor-specific data |
| 7 | Vendor 0xFF01 | 7 bytes (Feature report) | Vendor config |
| 8 | System Control | 1 byte (3 bits + padding) | Power/Sleep/Wake |

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

## Button State Preservation

### The Problem

Without button state tracking, injected commands would:
1. Send `report(0, dx, dy)` - OS sees all buttons released
2. Real mouse sends `report(buttons, 0, 0)` - OS sees buttons pressed again
3. This creates spurious press/release edges that break:
 - Drag operations
 - Latch-based features (mode_a, nonmode_a) on the PC side
 - Any hold-to-activate behavior

### The Solution

```cpp
// In exec():
uint8_t real = mouseParser.prevButtons; // current real button state

// All injected commands OR with real buttons:
Mouse.report(real | MOUSE_LEFT, x, y); // click preserves existing buttons
Mouse.report(real, -x, -y); // snapback restores real state
```

`prevButtons` is updated by `MouseRptParser::Parse()` on every real mouse report, always reflecting the actual physical button state.

## report() vs move()/press()/release()

### Standard Arduino Mouse Library

```
Mouse.press(MOUSE_LEFT); // Report 1: buttons=LEFT, x=0, y=0
Mouse.move(100, 50); // Report 2: buttons=LEFT, x=100, y=50
Mouse.release(MOUSE_LEFT); // Report 3: buttons=0, x=0, y=0
```
3 USB reports for one action. Intermediate states visible to OS.

### ImprovedMouse report()

```
Mouse.report(buttons, x, y, wheel); // Report 1: everything in one packet
```
1 USB report. Atomic. No intermediate states.

This is critical for the P (silent aim) command:
```
// Two reports, back-to-back:
Mouse.report(real | LEFT, x, y); // Move + click in one report
Mouse.report(real, -x, -y); // Snap back + release in one report
```
The OS sees a 1-frame click at the offset position. No visible movement.

## Timing

### Main Loop

```cpp
void loop() {
 // UDP checked FIRST for lowest command latency (saves 100-500µs vs old order)
 packetSize = Udp.parsePacket();
 if (packetSize) {
 Udp.read(...);
 parse(buffer); // Execute all commands in packet
 }
 
 Usb.Task(); // Poll USB Host for real mouse events (~125us)
}
```

No delays in loop. UDP commands processed before USB Host polling to minimize command latency.

### P Command - Atomic vs Split Mode

```
P_COOLDOWN = 200ms (configurable via 'K' command)
P_SPLIT_DELAY = 1250µs (configurable via 'D' command)

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

Split mode separates movement from click across USB frames, matching old firmware timing where `optimizedMove()` finished before `Mouse.click()` ran.

### Configuration Commands

| Command | Variable | Description |
|---------|----------|-------------|
| `D<value>\r` | `P_SPLIT_DELAY` | Set split delay in microseconds (0=atomic) |
| `K<value>\r` | `P_COOLDOWN` | Set cooldown in milliseconds |

Both sent by PC at startup and when changed via web UI.

## MAC Address Generation

```cpp
void genMAC() {
 randomSeed(analogRead(0) + micros()); // entropy from analog noise + timer
 mac[0] = 0xEE; // locally administered, unicast
 for (i = 1..5)
 mac[i] = random(256);
}
```

First byte `0xEE`:
- Bit 0 = 0: unicast (not multicast)
- Bit 1 = 1: locally administered (not globally unique)

Different MAC on every boot prevents network-level device tracking.

## Integer Parser

Custom `parseInt()` avoids `atoi`/`strtol` overhead:

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

Bit-shift multiplication: `(x << 3) + (x << 1)` = `x * 8 + x * 2` = `x * 10`. Faster than actual multiplication on AVR.
