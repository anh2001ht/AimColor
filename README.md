# Coloruino

Color-based combat assist for Valorant. Four parts: a PC application that sees the screen, an Arduino that pretends to be a mouse, a process-hollowing loader that ships the PC application as a single binary, and a one-off config generator used during install.

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

By default everything runs on ONE Windows PC. The Arduino plugs into that PC twice (USB for HID output, Ethernet for UDP input). Build the binaries on a SEPARATE PC for hygiene.

Optional 2PC hardening with a capture card: AMDRSHelper.exe runs on a second PC that sees the game PC's screen via the capture card; the Arduino bridges them. Vanguard sees nothing on the game PC. See [USER_GUIDE.md](USER_GUIDE.md) for the wiring.

---

## Which doc should I read?

| If you are... | Read this |
|---|---|
| New to the project | [USER_GUIDE.md](USER_GUIDE.md) - plain-language tour |
| Building from source | [BUILD_GUIDE.md](BUILD_GUIDE.md) |
| Curious how it works internally | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Worried about detection | [SECURITY.md](SECURITY.md) |
| Working inside one component | the README.md inside that component's folder |

---

## The four binaries at a glance

```
coloruino-loader/    ->  AMDRSHelper.exe
                         The only binary that ever lands on the
                         play PC. Prompts for license once,
                         caches into auth.dat, decrypts and
                         hollows pipanel.exe into a random
                         target process.

coloruino-app/       ->  pipanel.exe
                         The actual cheat. Lives inside
                         AMDRSHelper.exe as encrypted bytes,
                         decrypted in RAM at runtime, never
                         hits the play PC's disk in plaintext.
                         Hosts WebUI on :13548 for live tuning.

coloruino-fw/        ->  coloruino-fw.ino  (Arduino)
                         Receives DNS-shaped UDP, executes
                         M/P/F/L command set as USB HID mouse
                         reports. Passes through real mouse
                         buttons via the USB Host Shield.

coloruino-config-    ->  config_generator.exe
generator/               One-off CLI that writes the encrypted
                         `data` file. In the current setup the
                         loader folds this in (data_writer.cpp)
                         and the generator is supplier-side
                         debug only.
```

---

## Hardware you need

- One Arduino Leonardo (or 32U4-compatible board).
- One USB Host Shield 2.0.
- One W5500 Ethernet shield.
- 6 jumper wires for the SPI sandwich (W5500 SPI lines routed past the USB Host Shield to the Arduino ICSP header).
- Two USB cables.
- One Ethernet cable.

The wiring is the only tricky part. Detailed photos + assembly steps in the linked UC thread referenced from [USER_GUIDE.md](USER_GUIDE.md).

---

## Quick start (default 1PC, secrets already rotated and binaries already built)

On your play PC:

1. Copy `AMDRSHelper.exe` somewhere. `C:\Program Files\AMD\CNext\CNext\` blends well.
2. Plug the Arduino into the PC: USB cable to a free port (HID mouse output), Ethernet cable to a spare NIC (UDP input).
3. Set a static IP on that spare NIC matching the firmware's subnet.
4. Double-click `AMDRSHelper.exe`. Enter the license once when prompted. Mouse twitches briefly = alive.
5. Subsequent launches don't prompt (auth.dat persists next to the exe).

To tune from your phone: open `http://<play-pc-ip>:13548/` in a browser, enter the WebUI Basic-auth credentials you rotated.

---

## Repository layout

```
coloruino/
+--- README.md             (you are here)
+--- USER_GUIDE.md         plain-language walkthrough
+--- BUILD_GUIDE.md        exhaustive build/sign/deploy
+--- ARCHITECTURE.md       system-wide technical
+--- SECURITY.md           threat model
+--- rotate_secrets.py     rotate license / keys / WebUI creds
+--- post.md               UC release writeup
|
+--- coloruino-app/        C++ Win32 application (pipanel.exe)
+--- coloruino-loader/     C++ process-hollowing loader (AMDRSHelper.exe)
+--- coloruino-fw/         Arduino Leonardo firmware
```

---

## Pointers

- The terms "silent aim" (`mode_a`), "flicker" (`nonmode_a`), "aimbot" (`apply_delta`) refer to specific modes in this app. See [ARCHITECTURE.md](ARCHITECTURE.md) for the glossary.
- The Arduino firmware relies on a PATCHED Leonardo board profile (USB VID/PID changed to mimic a real gaming mouse). See [coloruino-fw/README.md](coloruino-fw/README.md) for the patch.
- WebUI is intentionally branded "Spotify Web Player" inside the HTML, but the binary metadata is "AMD Radeon Software Helper". Split identity is deliberate, see [SECURITY.md](SECURITY.md).
- Constants are duplicated between the app and the loader (HWID salt, license hash key, config XOR key) because the loader must produce a `LICENSE_HWID` byte-identical to what the app computes at startup. If you rotate either, update BOTH sides. `rotate_secrets.py` reminds you of this.
- Don't rotate the license alone - license + AES build salt + per-machine `data` file are linked. Use `rotate_secrets.py` end-to-end.
- **Silent aim and flickbot are HIGH-RISK modes.** See [SECURITY.md](SECURITY.md) for why. Use sparingly or not at all.

---

## Licence / use

For personal research and educational reference. Don't redistribute as a paid service. Don't use against humans who didn't consent. [SECURITY.md](SECURITY.md) documents what this does and doesn't hide from Vanguard.
