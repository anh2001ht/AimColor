# Coloruino - Build Guide

Exhaustive technical pipeline for going from source to deployed binaries.
This is the operations doc - if you want plain-language setup instructions,
read [USER_GUIDE.md](USER_GUIDE.md) instead.

---

## Toolchain prerequisites

### Build machine

| Tool | Version | Why |
|---|---|---|
| Windows 10/11 x64 | any recent build | Host OS for MSVC |
| Visual Studio 2022 | 17.x with C++ Desktop workload | Compiles app, loader, config-generator |
| Windows 10 SDK | 10.0.19041+ | Headers, signtool.exe |
| MSVC v143 toolset | bundled with VS2022 | C++17 with /MT runtime |
| Python | 3.10+ | gen_build_secrets.py, sanitize_pe.py, encrypt_payload_aes.py, rotate_secrets.py |
| PowerShell | 5.1+ (built-in) | Signing scripts |
| VMProtect | 3.5.x with SDK | Packs the released binaries |
| HxD | any recent | Binary -> C-array dump for the loader payload |
| Arduino IDE | 1.8.19 or 2.x | Flashes firmware to the Leonardo |

### Client PC (single-PC deployment - the box that runs Valorant)

| Tool | Version | Why |
|---|---|---|
| Windows 10/11 x64 | any recent build | Runs the loader and Valorant |
| PowerShell | 5.1+ | One-time admin shell for installing the signing root cert |
| Microsoft Visual C++ Redistributable 2015-2022 | x64 | The app is /MT so the redist isn't strictly required, but having it avoids edge-case issues |
| AnyDesk (or equivalent) | recent | Inbound - optional - if you are remoting in to deploy to someone else, AnyDesk lets you enter the license without the recipient seeing it |

No persistent tooling is required after install. You ship (or copy yourself)
`AMDRSHelper.exe` (permanent) + `config_generator.exe` (deleted after
setup), plus the pre-flashed Arduino as a physical device.

---

## One-time build-machine setup

### 1. Clone the repository

```
git clone <repo> C:\Users\<you>\Desktop\coloruino-extra\coloruino
```

The build pipeline assumes paths under
`C:\Users\<you>\Desktop\coloruino-extra\coloruino`. Adjust the scripts if
you put it elsewhere - most use `$PSScriptRoot` so they work in-place.

### 2. Patch the Arduino AVR core

Coloruino's firmware spoofs a placeholder vendor USB mouse. The Arduino IDE
ships with the Leonardo profile pointing at the real Leonardo VID/PID,
which is fingerprintable. Patch in-place:

Open
`%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\boards.txt`
and add a NEW board entry (don't overwrite the existing Leonardo one):

```ini
leonardo_mod.name=Arduino Leonardo (MOD)
leonardo_mod.vid.0=0x????   # PLACEHOLDER: pick a real gaming-mouse VID
leonardo_mod.pid.0=0x????   # PLACEHOLDER: pick a real gaming-mouse PID
leonardo_mod.upload.tool=avrdude
leonardo_mod.upload.protocol=avr109
leonardo_mod.upload.maximum_size=28672
leonardo_mod.upload.maximum_data_size=2560
leonardo_mod.upload.speed=57600
leonardo_mod.upload.disable_flushing=true
leonardo_mod.upload.use_1200bps_touch=true
leonardo_mod.upload.wait_for_upload_port=true
leonardo_mod.bootloader.tool=avrdude
leonardo_mod.bootloader.low_fuses=0xff
leonardo_mod.bootloader.high_fuses=0xd8
leonardo_mod.bootloader.extended_fuses=0xcb
leonardo_mod.bootloader.file=caterina/Caterina-Leonardo.hex
leonardo_mod.bootloader.unlock_bits=0x3F
leonardo_mod.bootloader.lock_bits=0x2F
leonardo_mod.build.mcu=atmega32u4
leonardo_mod.build.f_cpu=16000000L
leonardo_mod.build.vid=0x????
leonardo_mod.build.pid=0x????
leonardo_mod.build.usb_product="PLACEHOLDER PRODUCT"
leonardo_mod.build.usb_manufacturer="PLACEHOLDER_MFR"
leonardo_mod.build.board=AVR_LEONARDO
leonardo_mod.build.core=arduino
leonardo_mod.build.variant=leonardo
leonardo_mod.build.extra_flags={build.usb_flags} -DCDC_DISABLED
```

Then in
`%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\cores\arduino\USBDesc.h`:

```c
#define IPRODUCT 1
#define IMANUFACTURER 2
#define ISERIAL 0
```

And in
`%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\cores\arduino\USBCore.cpp`,
ensure the device descriptor reads:

```c
// class subclass protocol packetSize0 VID PID bcdDevice
DeviceDescriptor PROGMEM = D_DEVICE(0x00, 0x00, 0x00, 8, USB_VID, USB_PID, 0x100, ...);
```

(class/subclass/protocol all 0x00, packetSize0 = 8 - that's what real
placeholder vendor mice report.)

### 3. Install VMProtect

Install VMProtect 3.5.x. Copy `VMProtectSDK64.lib` and `VMProtectSDK64.dll`
into `coloruino-app/coloruino5500/Source/Windows/` and
`coloruino-loader/Process Hollowing/Source/Windows/` (the
`Source\Windows` library path is referenced from each `.vcxproj`).

The loader also ships with a stub `VMProtectSDK.h` that no-ops the
SDK markers - the loader is NOT VMProtect-packed in the homebrew
setup; the markers are kept in source as comments. The PC application
IS VMProtect-packed.

### 4. Generate the signing certificate

```powershell
cd <repo-root>\tools\signing
.\01_generate_cert.ps1
```

Produces `code_signing.pfx`, `code_signing.cer`, `code_signing.password.txt`
in `tools/signing/`. The `.pfx` and `.password.txt` are gitignored - 
verify before committing.

---

## Build sequence (per release)

The pipeline has dependencies: you must build `coloruino-app` first
(its packed binary is the loader's payload), then `coloruino-loader`,
then `coloruino-config-generator`. The firmware is independent.

### Stage 1 - Rotate secrets (optional but recommended per release)

```
cd <repo-root>
python rotate_secrets.py
```

Prints fresh values for:
- License key (32 hex chars)
- WebUI Basic auth user + password
- Config XOR key (24 hex / 12 bytes)
- License hash key (18 ASCII)
- Protocol XOR key (16 bytes)

Plus per-key instructions on which source files to update. Paste the
new values into the listed files. After this stage, source is at the
new rotation.

> `rotate_secrets.py` does NOT touch source files itself - it just prints.
> You paste manually. This is intentional: you can run it as often as
> you want to preview different random sets, only commit when you're
> happy.

### Stage 2 - Build coloruino-app

The PC application. Visual Studio 2022 -> open
`coloruino-app/coloruino5500.sln` -> Release | x64.

**Project settings worth verifying** (`coloruino5500.vcxproj`):

| Setting | Value | Reason |
|---|---|---|
| SubSystem | Windows | No console window (silent run) |
| EntryPointSymbol | mainCRTStartup | Standard CRT entry calling `main()` |
| RuntimeLibrary | MultiThreaded (/MT) | No CRT DLL dependency at runtime |
| GenerateDebugInformation | No (Release) | No PDB shipped |
| UACExecutionLevel | (not set) | App doesn't require elevation |
| LibraryPath | Source\Windows;$(LibraryPath) | Finds VMProtectSDK64.lib |
| TargetName | pipanel | Output is pipanel.exe |

Build. Output lands at `coloruino-app/coloruino5500/x64/Release/pipanel.exe`.

### Stage 3 - VMProtect pack the PC application

Open VMProtect GUI. Open `pipanel.exe`. The source has VMProtect markers
(`VMProtectBeginUltra`, `VMProtectBeginMutation`) inside the C++ - 
VMProtect picks these up automatically.

Settings:
- Compilation type: Mutation + Ultra (use defaults)
- Watermark: optional
- Output: overwrite pipanel.exe in place

Click "Compile". Output: the same `pipanel.exe` path, now packed.

### Stage 4 - Convert packed binary to C array

The loader embeds the packed app binary as a `static const uint8_t[]`
literal. Use HxD:

1. Open the packed `pipanel.exe` in HxD.
2. `Edit -> Select All`.
3. `Edit -> Copy as -> C` -> "Unsigned char array".
4. Paste the result into
 `coloruino-loader/Process Hollowing/TabTip32_exe_bytes.h`, replacing
 the existing array.

Required structure of `TabTip32_exe_bytes.h`:

```c
#pragma once
#include "VMProtectSDK.h"

#include <cstdint>

static uint8_t TabTip32_exe[] = {
 0x4D, 0x5A, 0x90, 0x00, /* ... thousands of bytes ... */
};

static const size_t TabTip32_exe_len = sizeof(TabTip32_exe);
```

(The `static` not `static const` is intentional - the loader decrypts
the array in place at runtime.)

### Stage 5 - Generate per-build secrets

```
cd "coloruino-loader/Process Hollowing"
python gen_build_secrets.py
```

Produces `build_secrets.h` containing:
- `kBuildSalt[32]` - random 32 bytes added to the license during AES key derivation
- `kPayloadIV[16]` - random IV for the AES-CBC payload encryption

These are regenerated only if `build_secrets.h` is missing. To force
fresh: delete the file first.

> The vcxproj has a PreBuildEvent that runs this automatically if the
> header is missing. You usually don't need to invoke it manually.

### Stage 6 - Encrypt the embedded payload

```
python encrypt_payload_aes.py <license-key>
```

Reads `TabTip32_exe[]` from `TabTip32_exe_bytes.h`, derives key =
`SHA256(license || kBuildSalt)`, encrypts via AES-256-CBC with
`kPayloadIV`, writes the ciphertext back into the header - replacing
the plaintext bytes in place.

The license key used here MUST match what the user will type into the
loader's dialog at runtime, AND must match the FNV-1a constant in
`license.cpp:24`. If you rotated in Stage 1, use the new key.

> After this stage, the `TabTip32_exe[]` array contains ciphertext.
> Don't open it in HxD again unless you're starting over.

### Stage 7 - Build coloruino-loader

Open `coloruino-loader/Process Hollowing.sln`. Release | x64. Build.

**Project settings worth verifying** (`Process Hollowing.vcxproj`):

| Setting | Value | Reason |
|---|---|---|
| TargetName | AMDRSHelper | Output is AMDRSHelper.exe |
| SubSystem | Windows | License dialog appears, no console |
| RuntimeLibrary | MultiThreaded (/MT) | Self-contained |
| LibraryPath | Source\Windows;$(LibraryPath) | VMProtect SDK headers if used |
| PreBuildEvent | python gen_build_secrets.py | Idempotent |
| PostBuildEvent | python sanitize_pe.py "$(TargetPath)" | Zeros Rich header / timestamp / debug dir, randomizes section names, recalculates PE checksum |

Output: `coloruino-loader/Process Hollowing/x64/Release/AMDRSHelper.exe`.

The PostBuildEvent runs `sanitize_pe.py` automatically - you'll see its
output in the build log. If something else is hooked to that event,
ensure `sanitize_pe.py` runs LAST (rewrite anything else, then sanitize).

### Stage 8 - Build coloruino-config-generator

Open the config-generator project, build Release | x64. Output:
`config_generator.exe`. This is a tiny console binary that takes the
license as `argv[1]` and writes the encrypted `data` file.

### Stage 9 - Sign both binaries

```powershell
cd <repo-root>\tools\signing

.\02_sign_binary.ps1 "..\..\coloruino-loader\Process Hollowing\x64\Release\AMDRSHelper.exe"
.\02_sign_binary.ps1 "..\..\coloruino-app\coloruino5500\x64\Release\pipanel.exe" -Description "AMD Radeon Settings"
.\02_sign_binary.ps1 "..\..\coloruino-config-generator\<output-path>\config_generator.exe" -Description "AMD Radeon Configuration Tool"
```

Each invocation:
- Auto-discovers `signtool.exe` from your Windows SDK install.
- Reads `code_signing.pfx` + `code_signing.password.txt` from the
 same directory.
- Signs with SHA-256 + the `AMD Radeon Software` cert.
- Sets the file description shown in Properties -> Digital Signatures.

> Why sign AFTER VMProtect: VMProtect rewrites the binary, which would
> invalidate any signature applied before packing. Sign last.

> Why sign AFTER sanitize_pe: sanitize_pe also rewrites the binary. Sign
> after the loader's sanitize stage. (For pipanel.exe there's no sanitize
> step - but the same rule holds: sign last.)

> Why not bake into the vcxproj as a PostBuildEvent: VMProtect happens
> outside the VS build, and the build event would fire BEFORE VMProtect.
> Signing is a manual stage in the release procedure.

### Stage 10 - Flash the firmware

Open `coloruino-fw/coloruino-fw.ino` in Arduino IDE.
Tools -> Board -> **Arduino Leonardo (MOD)**. Tools -> Port -> the right COM.
Click Upload.

Verify the host PC sees the Arduino as "<your placeholder product>" by "<your placeholder mfr>"
in Devices and Printers. If it shows as "Arduino Leonardo", the patch
to the AVR core didn't take - recheck Step 2 of one-time setup.

### Stage 11 - Verify build artifacts

```
file or path expected
---------------------------------------------------------------------
coloruino-loader/.../AMDRSHelper.exe Authenticode-signed, packed
coloruino-app/.../pipanel.exe Authenticode-signed, packed
config_generator.exe Authenticode-signed
build_secrets.h fresh per build (kBuildSalt + kPayloadIV)
TabTip32_exe_bytes.h AES-encrypted ciphertext of packed pipanel.exe
auth.dat NOT present (created at first run)
data NOT present (created by config_generator)
```

---

## Deployment to the play PC

Default setup runs on the play PC directly. **Single-
binary deployment** - only `AMDRSHelper.exe` is permanent on the
client. The loader writes both `auth.dat` (license cache) and `data`
(app config with HWID-bound LICENSE_HWID) itself on first successful
license entry.

Detailed plain-language walkthrough in [USER_GUIDE.md](USER_GUIDE.md);
this is the technical reference.

1. Stage onto The play PC (drop somewhere innocuous - 
 `C:\Program Files\AMD\CNext\CNext\` is a good blend):
 - `AMDRSHelper.exe` - **permanent**
 - `code_signing.cer` - only if the client hasn't trusted the cert yet
 - `03_install_root_cert.ps1` - same

2. (First-time per client only) Install the root cert in an admin
 PowerShell:
 ```powershell
 cd <staging folder>
 .\03_install_root_cert.ps1
 ```
 Expect two green "Imported into ..." lines. Remove the .cer +
 .ps1 files after.

3. Launch:
 ```cmd
 AMDRSHelper.exe
 ```
 The license dialog appears. **You** paste the license, click OK.
 The dialog closes, the cursor twitches briefly (cheat alive), and
 the loader writes:
 - `auth.dat` - HWID-encrypted license cache (so subsequent launches
 don't prompt).
 - `data` - XOR-encrypted config seed with `LICENSE_HWID=<hash>`
 bound to this machine. The app picks up its compiled-in defaults
 for everything else.

4. Test via the WebUI: open `http://localhost:13548/` in any browser
 on the play PC. Hit "Test" - cursor should twitch.

5. You're done. The license sits in auth.dat encrypted to this machine's HWID.

> If the `data` file already exists when the loader runs (e.g. from a
> previous install), it's left alone - the loader only creates `data`
> if it's missing. This preserves any tweaks the client made via the
> WebUI.

> For debugging where you want to force-regenerate
> `data` without using the loader, `config_generator.exe` still works
> as before. Build it the same way you build the app, run it manually
> with the license as an argument. Not part of normal client
> deployment.

---

## Deployment to the Arduino

Already done in Stage 10 above. Once flashed, the Arduino runs the
firmware on every power-up. on the play PC, plug both:
- USB cable: Arduino -> client's USB port (HID mouse output to the OS
 Valorant runs on)
- Ethernet cable: Arduino's W5500 shield -> client's spare NIC (UDP
 command input from the hollowed PC application)

The play PC's NIC connected to the Arduino must have a static IP on
the same subnet as the Arduino's shield (default subnet
`192.168.1.0/24`, client picks something like `192.168.1.10`).

---

## Rotation procedures

### Rotating just the license

1. Generate or pick a new 32-hex license.
2. Update three source files (FNV-1a hash in each):
 - `coloruino-loader/Process Hollowing/license.cpp:24` (the literal inside `ct_fnv1a`)
 - `coloruino-app/coloruino5500/src/security/LicenseManager.cpp:35` (the literal inside `ct_fnv1a`)
 - `coloruino-config-generator/config_generator.cpp:29` (`VALID_LICENSE`)
3. Rebuild all three binaries.
4. Re-encrypt the payload:
 ```
 python encrypt_payload_aes.py <new-license>
 ```
5. Rebuild the loader (so the new ciphertext is embedded).
6. Re-sign all binaries.
7. On each play PC: delete the old `data` and
 `auth.dat`, redeploy `AMDRSHelper.exe`, launch loader, enter the
 new license. The loader writes a fresh `auth.dat` and `data` pair
 in one step.

### Rotating the signing identity

See `tools/signing/README.md`. Summary:

1. Delete `code_signing.pfx`, `code_signing.cer`, `code_signing.password.txt`.
2. Re-run `01_generate_cert.ps1`.
3. Re-sign all binaries (`02_sign_binary.ps1`).
4. On the play PC, uninstall the OLD cert thumbprint from
 `LocalMachine\Root` + `LocalMachine\TrustedPublisher`, re-run
 `03_install_root_cert.ps1` with the new `code_signing.cer`.

### Rotating the WebUI credentials

Update `xorstr_("...")` literals in
`coloruino-app/coloruino5500/src/security/Auth.cpp:62-63`.
Rebuild app -> re-VMProtect -> re-embed in loader -> re-sign.

### Rotating XOR / protocol keys

See `rotate_secrets.py` output. It prints the exact files + lines.
After updating, rebuild the affected components AND re-flash the
firmware (for protocol key changes).

---

## Files reference

### Source-controlled secrets (commit-safe)

- FNV-1a constants in `license.cpp`, `LicenseManager.cpp`,
 `config_generator.cpp` - these are hashes, not the keys themselves.
- XOR keys in `ConfigManager.cpp`, `hwid.cpp`, `Auth.cpp`,
 `UDPClient.cpp`, `coloruino-fw.ino` - visible in binary anyway, but
 decryptable only by attacking the running process.

### Build-time secrets (NEVER commit)

- `tools/signing/code_signing.pfx`
- `tools/signing/code_signing.password.txt`
- `coloruino-loader/Process Hollowing/build_secrets.h` (regenerable)
- Any local copies of the license key

### Per-machine artifacts (NEVER commit, ship with binary)

- `auth.dat` - generated next to AMDRSHelper.exe at first run
- `data` - generated next to AMDRSHelper.exe by config_generator

These two are encrypted to the target machine's HWID. Moving them to
another machine does nothing (decrypt fails silently -> re-prompt
license / re-run config_generator).

### Shareable distribution artifacts

- `tools/signing/code_signing.cer` (public cert - needed by 03_install_root_cert.ps1)
- `tools/signing/03_install_root_cert.ps1`

You can publish these on a webpage or share via any normal channel.

---

## Common failure modes

### "encrypt_payload_aes.py says 'kBuildSalt not found'"

`build_secrets.h` doesn't exist yet. Run `gen_build_secrets.py` first
(or build the loader once - the PreBuildEvent does it).

### "Loader builds but crashes on launch"

Almost always a payload mismatch: the encryption key the loader derives
doesn't match what was used to encrypt. Common causes:
- You rebuilt `build_secrets.h` between encrypt and loader build ->
 `kBuildSalt` changed -> derived key mismatches -> decryption produces
 garbage -> process hollowing crashes.
 Fix: `python encrypt_payload_aes.py <license>` again, then rebuild
 the loader without regenerating `build_secrets.h`.
- The license you encrypted with doesn't match the FNV-1a constant in
 `license.cpp`. Fix: rotate to a consistent value.

### "Loader runs but the application doesn't"

The loader picks a random target process to hollow into; some targets
fail. The retry loop (up to 8 attempts) handles this - make sure you're
on the patched build with the retry loop in `Process_Hollowing.cpp`.
If still failing, the embedded payload is corrupt (re-do Stage 4 and
re-encrypt) or VMProtect packed something that's incompatible with
hollowing (test without VMProtect first to isolate).

### "Loader keeps prompting for the license every launch"

`auth.dat` not persisting. The fixed code stores `auth.dat` next to the
exe (resolved via `GetModuleFileNameW`), not in the CWD. Rebuild from
post-fix source. If still happening after fix: HWID instability
(`first_mac` picking a different adapter each run) - see
[SECURITY.md](SECURITY.md) for the planned mitigation.

### "signtool: SignTool Error: No certificates were found"

The `.pfx` file got deleted or wasn't generated. Re-run
`01_generate_cert.ps1`. If you're rotating, also clean the cert stores
on target machines first.

### "SmartScreen still warns even though I signed it"

The target machine hasn't run `03_install_root_cert.ps1`. Self-signed
certs don't validate against Windows's built-in chain - they need the
public cert installed in `LocalMachine\Root` and
`LocalMachine\TrustedPublisher`.

### "Defender quarantines the binary right after signing"

Defender may flag based on heuristics (entropy, packed sections, etc.)
independent of signature. Either add a folder exclusion on the target
machine, or accept the trade-off and let it quarantine the first build
while you whitelist via Defender's UI.

### "Firmware compiles but the Arduino shows as Arduino Leonardo, not USB GAMING MOUSE"

The board profile patch (one-time setup step 2) didn't apply. Verify
in Tools -> Board which entry you selected. If you see only "Arduino
Leonardo" and no "Arduino Leonardo (MOD)" entry, `boards.txt` didn't get
the new entry. Reapply.

---

## Pre-flight checklist

Before declaring a release done:

- [ ] `rotate_secrets.py` run, all values pasted into source.
- [ ] App built fresh Release|x64.
- [ ] App VMProtect-packed.
- [ ] `pipanel.exe` dumped to `TabTip32_exe_bytes.h` via HxD.
- [ ] `gen_build_secrets.py` (or PreBuildEvent) produced `build_secrets.h`.
- [ ] `encrypt_payload_aes.py <license>` re-encrypted the array.
- [ ] Loader rebuilt fresh Release|x64.
- [ ] `sanitize_pe.py` ran via PostBuildEvent (check build log).
- [ ] Config-generator rebuilt fresh Release|x64.
- [ ] All three binaries signed with `02_sign_binary.ps1`.
- [ ] Firmware reflashed (only if rotation included protocol key).
- [ ] On play PC: deleted any old `auth.dat` and `data`, ran
 `config_generator.exe`, launched loader, entered license.
- [ ] WebUI reachable from phone at `http://<PC1>:13548/`.
- [ ] Test cursor twitch via WebUI's "Test" button.
- [ ] Live aim test against a Custom game enemy.
