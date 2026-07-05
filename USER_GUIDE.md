# Coloruino - User Guide

Plain-language walkthrough. No code, no jargon you can't skip. For the technical doc see [ARCHITECTURE.md](ARCHITECTURE.md). For exhaustive build steps see [BUILD_GUIDE.md](BUILD_GUIDE.md).

---

## What is this thing

A small program watches your screen. When an enemy outline shows up in purple (Valorant's default enemy highlight), the program decides where to aim, and tells a small electronics board called an Arduino what to do. The Arduino plugs into your PC and pretends to be a USB mouse. From the OS's and Valorant's perspective, an ordinary gaming mouse is doing all the moving.

There is no DLL injected into Valorant, no driver, no in-game memory access, no kernel module. The cheat code never touches the game's process. Input arrives the same way it would from any USB mouse.

---

## Two deployment options

### 1PC default (what most people use)

Everything runs on ONE Windows PC. Valorant runs there, the cheat runs there, the Arduino is plugged into that PC. Simpler setup, less hardware, less cost. The trade-off is that Vanguard CAN see the cheat process and the WebUI listener on the same box - the design makes them look as innocuous as possible (sanitized loader, AMD-branded metadata, signed binary, hollowed into a legitimate target process) but they're observable.

### 2PC optional (more secure, more hardware)

Two PCs: a GAME PC running Valorant + Vanguard, and a CHEAT PC running the cheat application. A capture card on the CHEAT PC reads the GAME PC's display via HDMI. The Arduino plugs into the GAME PC for USB HID output but receives UDP from the CHEAT PC via Ethernet.

Result: Vanguard scans the GAME PC and finds NOTHING related to coloruino. No process, no DLL, no listening socket, no suspicious binaries. The trade-off is the cost of a second PC + a passthrough capture card + more cables.

The software is identical in both setups. This guide covers 1PC; for 2PC just put `AMDRSHelper.exe` on the cheat box, give the cheat box's NIC an IP on the Arduino's subnet, and plug the Arduino's USB into the game box.

---

## Build PC vs play PC

You should ALWAYS build on a separate PC from the one you play on. The build PC has Visual Studio, Python, the Arduino IDE, HxD, optionally VMProtect. None of that should sit on your play PC. Build on box A, copy `AMDRSHelper.exe` to box B, done.

This is true even in 1PC mode for the play half. The "1PC" refers to your play box; the build box is a separate concern.

---

## What you need

**Hardware:**

- One Arduino Leonardo (or any ATmega32U4 board).
- One USB Host Shield 2.0.
- One W5500 Ethernet shield.
- 6 short jumper wires.
- Two USB cables (one for Arduino programming/HID, one for real mouse passthrough).
- One Ethernet cable from your PC's NIC to the W5500.

**Software:**

- `AMDRSHelper.exe` (the only binary that ends up on your play PC).
- The Arduino firmware flashed onto the board (done at build time, not at install time).

**Knowledge:**

- A 32-character license key (lowercase hex). You set this at build time.
- The WebUI Basic-auth username + password you set at build time.

---

## Hardware setup

Wiring is the only tricky part. The W5500 needs SPI but the USB Host Shield's pin header physically blocks the Arduino's ICSP pins. Solution: sandwich jumper wires from the Arduino's ICSP UP past the USB Host Shield to the W5500.

Detailed photos + step-by-step on my older UC thread:

> **https://www.unknowncheats.me/forum/4078059-post1.html** - hardware assembly + wiring guide with visuals.

And the general background on Arduino-based cheat hardware (older but conceptually correct):

> **https://www.unknowncheats.me/forum/4093793-post1.html** - all-in-one Arduino cheating guide.

Pin assignments the firmware expects:

| W5500 pin | Arduino pin |
|-----------|-------------|
| MOSI | ICSP-4 |
| MISO | ICSP-1 |
| SCK | ICSP-3 |
| 5V | 5V |
| GND | GND |
| CS | D6 |
| RST | D7 |

Once the stack is wired:

- Plug the Arduino's main USB cable into your PC. This is what acts as the spoofed gaming mouse (it's how Valorant gets HID input).
- Plug an Ethernet cable from the Arduino's W5500 to a spare NIC on your PC. This is how the cheat app sends UDP commands.
- Plug your REAL mouse into the USB Host Shield's USB port (NOT directly into your PC). The firmware passes its button state through.
- Set a static IP on your spare NIC matching the Arduino's subnet (default placeholder 192.168.1.x; firmware ships with 192.168.1.216).

---

## First-time install

1. Copy `AMDRSHelper.exe` somewhere on your play PC. Where doesn't matter much; `C:\Program Files\AMD\CNext\CNext\` blends well with real AMD installs.
2. (Optional) Install the signing root cert if your build chain produced one. Right-click `AMDRSHelper.exe` -> Properties -> Digital Signatures - should show "AMD Radeon Software" as the publisher with no warning after cert install. Without it, Defender treats the signed binary the same as unsigned.
3. Double-click `AMDRSHelper.exe`. A small dialog appears asking for the license key. Paste it. Click OK.
4. If everything is wired up:
   - The dialog disappears.
   - Your mouse cursor twitches very briefly (the application's "I'm alive" signal).
   - Two files appear next to the exe: `auth.dat` (the encrypted remembered license) and `data` (the encrypted config with your HWID hash baked in).
5. Open `http://localhost:13548/` in a browser. Enter the WebUI Basic-auth creds you set at build time. You should see the config panel.
6. Hit the **Test** button at the top of the panel. Your mouse cursor should twitch once - that confirms the whole chain works (cheat app -> UDP -> Arduino -> HID -> your OS).

---

## Daily use

After install:

1. Turn on your PC.
2. Double-click `AMDRSHelper.exe` (or put it in the Startup folder so it launches with Windows). No license prompt - `auth.dat` handles it.
3. Make sure the Arduino is plugged in (USB + Ethernet).
4. Start Valorant.
5. Play.

To adjust on the fly, open the WebUI on phone or browser, slide the sliders. Changes save instantly. No restart needed.

To temporarily disable a mode, open the WebUI -> the mode's tab -> flip "Enabled" off.

---

## The WebUI - what each section does

### Aimbot tab (LOW risk)

Continuous tracking while a key is held. Smooth, blends into normal mouse movement.

- **Enabled** on/off.
- **FOV** search radius in pixels around the crosshair.
- **Smooth** higher = lazier tracking, more natural.
- **Speed** higher = faster pull toward target.
- **Sleep** milliseconds between adjustments.
- **Key 1 / Key 2** Windows virtual key codes (2=right mouse, 1=left, 6=middle, 65=A, etc.).

### Silent tab (HIGH risk)

One-shot snap-to-target + click + snap-back. The riskiest mode.

- **Head Targeting** pick top of silhouette vs closest pixel.
- **FOV / Distance gain / Cooldown** same idea as aimbot.

WARNING: Vanguard's mouse-event rolling-window analyzer flags concentrated burst-fire patterns. Silent aim fires 4 HID reports back-to-back with deterministic shape - that's a statistical signature. Use sparingly, with high cooldowns, and not for every engagement. See [SECURITY.md](SECURITY.md) for the full risk breakdown.

### Flicker tab (HIGH risk)

Like silent but without the snap-back. 3 HID reports. Same risk profile - see above.

### Trigger tab (MEDIUM risk)

Auto left-click when crosshair lands on enemy color. Aligned with deliberate key presses so less anomalous than silent/flick, but still automated.

- **FOV X / FOV Y** how big a box around the crosshair to check.
- **Polygon Check** when on, only fires if color is in the middle of an enemy shape (filters single-sided UI false fires).

### Head Anchor Refinement tab

Fine-tune WHERE on the enemy to aim (forehead vs nose vs neck vs chest). Most people leave this alone after finding a setting they like.

### Filtering tab

- **Dead Body Filter** reject ragdoll targets sliding off-screen.
- **Cluster Validation** reject isolated noise pixels.

### Color tab

Four color presets the LUT can be loaded with. Default is **Purple** (Valorant's enemy outline color). Variants for unusual lighting or skin tints.

### Performance tab

- **GPU compute** runs detection on the graphics card. Faster but limited to 255 px FOV per axis. On by default.
- **Arduino Connection** IP + port where the application sends UDP. Updates persist to `data` automatically. The application reconnects on change.

---

## Troubleshooting

### "I launched AMDRSHelper.exe and nothing happened"

Open Task Manager. Look for `AMDRSHelper.exe` under Background processes. If absent:

- Check the folder for `data` and `auth.dat`. If absent, the loader never finished first-run. Re-launch and enter the license.
- Windows Defender may have eaten it. Add a folder exclusion in Defender -> Virus & threat protection -> Manage settings -> Exclusions.
- If you skipped the cert install step and Defender's behaviour monitor is aggressive, the binary may be killed silently. Install the cert.

### "The loader keeps asking for the license every launch"

`auth.dat` isn't being read on subsequent launches. Should be fixed in current builds (the path is resolved relative to the exe location). If still happening, you're running an old build - rebuild from current source.

### "Cursor twitches but doesn't actually aim correctly"

- Wrong color preset for current map/agent? Test in a Custom game with bright purple enemy outlines.
- FOV too small for the engagement range you're shooting at?
- Smooth/Speed too low? Bump them up.
- Wrong head-anchor settings? Try the defaults.

### "The Arduino doesn't seem to respond"

- Both cables plugged in? USB to PC for HID, Ethernet to PC for UDP.
- In the URL bar of the WebUI, type `/testing` (e.g. `http://localhost:13548/testing`). The application sends a small move to the Arduino. If the cursor twitches, the chain works. If not, check the Arduino's static IP matches what the WebUI's Performance tab says.
- Real mouse plugged into the USB Host Shield (not directly into the PC)?

### "Valorant gave an anti-cheat error / account banned"

Stop using the tool on that account. Read [SECURITY.md](SECURITY.md) for the honest threat model. No setup is guaranteed safe forever - Vanguard updates, detection technology evolves.

If you were using silent or flickbot heavily, that's almost certainly the cause. They're flagged as HIGH risk for a reason.

### "Defender quarantined AMDRSHelper.exe"

Two options:

1. Add a folder exclusion (Defender -> Manage settings -> Add exclusion).
2. Make sure you installed the signing cert at install time - without it, Defender treats signed and unsigned the same.

### "The WebUI works on the PC but not on my phone"

Your router blocks phone-to-PC traffic. Common on corporate Wi-Fi, public networks, and some mesh routers (AP isolation). The PC's local browser at `http://localhost:13548/` always works.

### "I forgot the WebUI password"

It's whatever you set at build time (look at `coloruino-app/.../Auth.cpp` source for what you put). If you don't have the source handy you'll need to rebuild with new creds.

---

## FAQ

**Q: What's the license for?**

A: It's a key that unlocks decryption of the cheat code embedded inside `AMDRSHelper.exe`. Without the right license those bytes are random nonsense, the loader would unpack garbage and fail to inject anything. The license is HWID-bound via `auth.dat` so copying the binary to a different PC without re-entering the license gets you nothing usable.

**Q: Where does `data` come from?**

A: The loader writes it next to itself the first time a license is successfully entered. The format is identical to what `config_generator.exe` produces (encrypted with the config XOR key, LICENSE_HWID bound to this machine). The standalone `config_generator` still exists for supplier-side debug but isn't part of the regular install.

**Q: Is this guaranteed undetectable?**

A: No. No cheat is. [SECURITY.md](SECURITY.md) documents what we hide and what we don't. Vanguard updates regularly. Silent aim and flickbot are HIGH risk and HAVE gotten me banned.

**Q: Can I run this on a 1PC setup vs 2PC setup?**

A: Both work. 1PC is the default (one box runs everything). 2PC with a capture card is more secure but requires a second PC + capture card. See [ARCHITECTURE.md](ARCHITECTURE.md) for the 2PC wiring.

**Q: Does it auto-aim through walls?**

A: Only if the wall is purple. The system literally only sees the enemy outline color. Anything not outlined is invisible.

**Q: Does the application phone home?**

A: No. Everything is local. No telemetry, no license server, no auto-update. The only network traffic is local LAN (PC to Arduino UDP + WebUI listener).

**Q: What about latency?**

A: Total end-to-end latency from "pixel changes color" to "Arduino fires a mouse event" is typically 2-5 ms. Faster than human reaction time (200+ ms) but plausibly within real-mouse range.

**Q: I want to change the license / WebUI password / any of the keys**

A: Run `python rotate_secrets.py` on your build PC. It prints fresh values and the source files to paste them into. Rebuild, redeploy.

---

## Where to look next

- [BUILD_GUIDE.md](BUILD_GUIDE.md) - every flag, every config setting, every pre-flight check.
- [ARCHITECTURE.md](ARCHITECTURE.md) - how screen capture, color detection, network protocol, and process hollowing actually work.
- [SECURITY.md](SECURITY.md) - what Vanguard sees, what we defend against, what's residual risk. **Read the silent/flickbot risk section before flipping those on.**
- `coloruino-fw/README.md` - patching the Arduino IDE board profile to spoof the chosen real-mouse VID/PID/descriptor.

---

## When you get stuck

The safe recovery for most config-corruption issues is the clean cycle: delete `data`, delete `auth.dat`, relaunch `AMDRSHelper.exe`, enter the license again. Two minutes, fixes 90% of mystery failures.
