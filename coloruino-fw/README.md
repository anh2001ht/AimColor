# coloruino-fw - Arduino HID Firmware

Arduino firmware for USB HID mouse command injection with real mouse
passthrough. Receives UDP commands from Coloruino PC application and
injects them as legitimate USB HID reports while preserving the real
mouse's button state.

> **See also**:
> [Top-level README](../README.md), [USER_GUIDE](../USER_GUIDE.md),
> [BUILD_GUIDE](../BUILD_GUIDE.md), [ARCHITECTURE](../ARCHITECTURE.md),
> [SECURITY](../SECURITY.md).

> **Phase 6.1 state** (after aggressive responsiveness pass):
> - USB descriptor mimics real gaming mouse you mimic (VID 0x???? / PID 0x????).
> - MAC OUI rotates per boot from 6 vendor pool (Intel/Realtek/Apple/Samsung/Microsoft/Dell).
> - M command sub-stepping:
> - Max 5 sub-steps (was 8) - large flicks complete ~3 ms faster.
> - Sub-step threshold raised to <10 px (was <3 px) - most tracking
> micro-corrections fire as a single report, lower latency.
> - Inter-sub-step jitter: uniform 100-300 µs (was bimodal 150-405 µs
> + 6% × 1000-1255 µs). Bimodal slow mode dropped - added perceived
> sluggishness without much detection benefit at our existing
> host-observed cadence.
> - P (silent aim) and F (flicker) handlers UNCHANGED - no humanization.
> - L (click) hold time: uniform 20-67 ms.
> - Network protocol: DNS-shape UDP on port 5353 with XOR-encrypted base32 payload + CRC-8.
> - Main loop: UDP polled both BEFORE and AFTER Usb.Task() - catches
> packets that arrive during the 100-500 µs Usb.Task blocking window.
> - HID interface order forced (Mouse first, Keyboard second - matches real device descriptor order).
> - Compile flags: `-O2` (set via `build.extra_flags` in your patched
> boards.txt). Overrides Arduino's default `-Os`.
>
> **Trade-off vs `extra/mouse-polling-monitor` heuristics**: dropping the
> bimodal slow mode lowers host-observed std dev. On a clean test rig
> with no other USB traffic, the polling-monitor verdict may drop from
> NATURAL to SUSPICIOUS. Run the monitor against your live setup after
> flashing. If it stays NATURAL, you're fine. If it flips, revert the
> jitter block in `sub_step_move()` to the prior bimodal version.

---

## Hardware Setup

Default 1PC layout - the Arduino's USB output and W5500 Ethernet input
both connect to the SAME PC that runs Valorant and AMDRSHelper.exe.

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

- Arduino board (AVR architecture, e.g., Leonardo / Pro Micro - must have native USB).
- USB Host Shield 2.0 (for real mouse passthrough).
- W5500 Ethernet shield.
- Real USB mouse connected to the USB Host Shield port (NOT directly to PC).
- 6 jumper wires for the W5500 SPI sandwich (see top-level USER_GUIDE.md).

### Network Configuration

The firmware ships with PLACEHOLDER network defaults. Edit before flashing.

| Parameter | Placeholder default | Notes |
|-----------|---------------------|-------|
| IP        | 192.168.1.216       | The Arduino's static IP. PC's spare NIC must be on the same /24. |
| DNS       | 192.168.1.1         | Cosmetic; the Arduino doesn't do DNS lookups. |
| Gateway   | 192.168.1.1         | Cosmetic for the same reason. |
| Subnet    | 255.255.255.0       | /24, fine in nearly all home setups. |
| UDP Port  | 5353                | mDNS port; the cheat application sends DNS-shaped packets here. |
| MAC       | Random each boot (rotated from 6 real consumer-vendor OUIs) or fixed via `MAC_RANDOM false`. | |

---

## USB Device Identity

The Arduino presents itself as a real gaming mouse to the OS. The
released firmware ships with PLACEHOLDER identity values. You MUST
patch these to match a real gaming-mouse product before flashing:

| Field | Value (PLACEHOLDER, replace) |
|-------|--------|
| VID | 0x???? |
| PID | 0x???? |
| Manufacturer | "PLACEHOLDER_MFR" |
| Product | "PLACEHOLDER_PRODUCT" |

Set via `boards.txt` defines (`-DUSB_VID`, `-DUSB_PID`, etc.) and `#define` fallbacks.

Capture the device descriptor of YOUR OWN physical gaming mouse with
Wireshark/USBPcap or the Windows USB device viewer, and patch your
local Arduino AVR core's `USBCore.cpp` + `USBDesc.h` to match it byte
for byte (class, subclass, protocol, packetSize0, etc.).

---

## Dual HID Interface Architecture

The real mouse this firmware clones has two HID interfaces. Both are replicated:

### Interface 0 - Mouse (ImprovedMouse.h)

Primary mouse interface with boot protocol support.

**HID Report (6 bytes, Report ID 1):**
| Byte | Field | Type |
|------|-------|------|
| 0 | Buttons (5 buttons) | uint8_t bitmap |
| 1-2 | X axis | int16_t |
| 3-4 | Y axis | int16_t |
| 5 | Wheel | int8_t |

**Key method:** `Mouse.report(buttons, x, y, wheel)` sends a complete HID report in one USB packet. Unlike standard Arduino Mouse library methods (`move`, `press`, `release`), this avoids generating spurious intermediate reports.

### Interface 1 - Keyboard/Consumer/Vendor Stub (SecondHIDIface.h)

Descriptor-only stub. No data is ever sent. The host polls EP2 IN and receives NAK, which is normal behavior.

**Report descriptors (140 bytes total):**
| Report ID | Usage | Purpose |
|-----------|-------|---------|
| 3 | Keyboard | 8 modifier keys + 6 key codes |
| 2 | Consumer Control | Media keys (16-bit usage) |
| 6 | Vendor 0xFF00 | 2 bytes vendor data |
| 7 | Vendor 0xFF01 (Feature) | 7 bytes feature report |
| 8 | System Control | Power/Sleep/Wake (3 bits) |

This second interface exists purely to match the real mouse's USB descriptor fingerprint. Without it, the device would only have one interface, which could be distinguishable from the real hardware.

---

## Real Mouse Passthrough

### hidcustom.h - MouseRptParser

Parses incoming USB Host reports from the real mouse and forwards them through the Arduino's device interface.

**Report format parsed:** `[ReportID=1] [Buttons] [Xlo] [Xhi] [Ylo] [Yhi] [Wheel]`

**Optimizations over v1:**
- Single `Mouse.report()` call combines buttons + movement + wheel
- Only sends report if something actually changed
- Wheel no longer dropped when dx==0 && dy==0
- No separate button-change reports before movement reports

`prevButtons` is public so the command executor can read the real button state.

---

## Command Protocol

Commands arrive as UDP packets. Multiple commands can be semicolon-separated in a single packet.

### Format
```
<prefix><x>,<y>\r
```

### Commands

| Prefix | Name | Behavior | HID Reports Sent |
|--------|------|----------|-----------------|
| `M` | Move | Move mouse, preserve real buttons | 1: `report(real, x, y)` |
| `L` | Click | Left click (press + release) | 2: `report(real|LEFT, 0, 0)` then `report(real, 0, 0)` |
| `P` | Silent Aim | Move+click, then snapback+release | Atomic (split=0): 2 reports. Split (split>0): move -> delay -> click -> snapback (3-4 reports) |
| `F` | Flicker | Move+click, then release (stay) | 2: `report(real|LEFT, x, y)` then `report(real, 0, 0)` |
| `D` | Split Delay | Set P_SPLIT_DELAY | Configures µs delay between move and click in split P mode |
| `K` | Cooldown | Set P_COOLDOWN | Configures ms cooldown between P commands |

### Button State Preservation

Every injected command reads `mouseParser.prevButtons` to get the real mouse's current button state. This prevents:
- Spurious button release/press edges when injecting movement
- Breaking latch-based features (mode_a, nonmode_a) on the PC side
- OS seeing phantom button state changes

### Examples
```
M100,50 -> move 100 right, 50 down
L -> left click
P200,100 -> silent aim: move (200,100), click, snap back (-200,-100), release
F200,100 -> flicker: move (200,100), click, release
M10,0;L;M-10,0 -> chained: move, click, move back
```

---

## MAC Address

Two modes controlled by `#define MAC_RANDOM`:

| Mode | Behavior |
|------|----------|
| `true` (default) | Generate random MAC on each boot using `analogRead(0) + micros()` as seed. First byte always `0xEE`. |
| `false` | Use hardcoded `FIXED_MAC` array |

Random MAC prevents network-level device fingerprinting across reboots.

---

## Integer Parser

Custom `parseInt()` avoids stdlib overhead. Uses bit-shift multiplication: `value = (value << 3) + (value << 1) + digit` which equals `value * 10 + digit`.

Handles negative numbers via leading `-` sign.

---

## P Command Cooldown & Split Mode

Silent aim (`P`) has a configurable cooldown (`P_COOLDOWN`, default 200ms) to prevent rapid-fire. Configurable via `K` command from PC. Separate from PC-side 100ms debounce.

### Split P Mode

When `P_SPLIT_DELAY > 0` (configurable via `D` command), the P command splits into separate USB reports:

1. `report(real, x, y)` - movement only (no click)
2. `delayMicroseconds(P_SPLIT_DELAY)` - wait between USB frames
3. `report(real | LEFT, 0, 0)` - click at new position
4. `report(real, -x, -y)` - snapback + release

This matches old firmware behavior where `optimizedMove()` completed before `Mouse.click()` ran. Default: 1250µs (sits between two 1kHz USB polling intervals).

When `P_SPLIT_DELAY == 0`, atomic mode: move+click in one report, then snapback (2 reports total).

---

## Build

### Requirements
- Arduino IDE or PlatformIO
- Libraries: SPI, Ethernet, USBHost (USB Host Shield 2.0)
- Board with native USB (Leonardo, Pro Micro, etc.)

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
- `ImprovedMouse.h` replaces the standard Arduino Mouse library. The corresponding `Mouse_.cpp` must also be present (provides the HID report descriptor and `report()` implementation).
- `SecondHIDIface.h` must be instantiated exactly once at global scope before `USBDevice.attach()` runs.
- Registration order matters: ImprovedMouse registers first (Interface 0 / EP1), then SecondHIDIface (Interface 1 / EP2).
