# Build Guide

## Prerequisites

| Component | Version | Purpose |
|-----------|---------|---------|
| Visual Studio | 2022+ | MSVC v143 toolchain |
| Windows SDK | 10.0+ | Win32 API, DXGI, D3D11 |
| VMProtect SDK | 3.x | Headers only (VMProtectSDK.h) |
| xorstr | - | Compile-time string encryption header |

## Build Steps

### 1. Build Coloruino

1. Open solution in Visual Studio
2. Set configuration: **Release / x64**
3. Build solution
4. Output: `x64\Release\pipanel.exe` (or configured output name)

### 2. VMProtect Pass 1 (Coloruino)

Open the built executable in VMProtect.

| Setting | Value |
|---------|-------|
| Memory Protection | **OFF** |
| Import Protection | ON |
| Resource Protection | ON |
| Packer | ON |
| Strip Relocations | OFF |
| Strip Debug Info | ON |

**Critical:** Memory Protection must be OFF. It performs CRC integrity checks against the on-disk PE file. When the binary runs via process hollowing, the file on disk is a different process - CRC fails with E#F3-1.

Protect all VMProtect-marked functions:
- `generateHtml` (Mutation)
- `handleHttpRequest` (Mutation)

Output: packed `pipanel.exe` (VS project TargetName is `pipanel`;
the packed file overwrites the unpacked one in place).

### 3. HxD Byte Export

1. Open the packed `pipanel.exe` in HxD.
2. Edit -> Select All -> Edit -> Copy as -> C -> "Unsigned char array".
3. Paste into `coloruino-loader/Process Hollowing/TabTip32_exe_bytes.h`,
 replacing the existing array.
4. Verify: array named `TabTip32_exe`, length var `TabTip32_exe_len`.
 (The legacy filename and identifiers - `TabTip32_exe_bytes.h`,
 `TabTip32_exe`, `TabTip32_exe_len` - are retained for symbol
 compatibility with the loader code, even though the underlying
 binary is `pipanel.exe` and the loader is `AMDRSHelper.exe`.)

### 3.5. Encrypt Embedded Payload

```
cd "coloruino-loader/Process Hollowing"
python gen_build_secrets.py # produces build_secrets.h (kBuildSalt + kPayloadIV) if missing
python encrypt_payload_aes.py <license-key>
```

AES-256-CBC-encrypts the `TabTip32_exe[]` array in place using
key = `SHA256(license || kBuildSalt)`, IV = `kPayloadIV`. The loader
derives the same key at runtime from the user-entered license.

### 4. Build Loader (coloruino-loader)

1. The encrypted `TabTip32_exe_bytes.h` is already in the project from
 step 3.
2. Open `coloruino-loader/Process Hollowing.sln`.
3. Build: **Release / x64**.
4. PostBuildEvent runs `sanitize_pe.py` automatically (Rich header,
 timestamp, debug dir, section names, PE checksum).
5. Output: `x64\Release\AMDRSHelper.exe`.

### 5. VMProtect Pass 2 (ProcessHollowing)

> Optional in the current build - the homebrew anti-debug + WinAPI
> dynamic resolution + xorstr literals provide a baseline without
> VMProtect on the loader. If you ARE packing the loader, do it AFTER
> sanitize_pe (sanitize_pe runs as PostBuildEvent so it's already done
> when you open the binary in VMProtect).

Open `AMDRSHelper.exe` in VMProtect.

| Setting | Value |
|---------|-------|
| Memory Protection | **ON** |
| Import Protection | ON |
| Resource Protection | ON |
| Packer | ON |

Protect all VMProtect-marked functions:
- `RP32`, `RP64`, `RPR32`, `RPR64` (Ultra)
- `FRT` (Mutation)
- `MainFunction` (Ultra)

Output: final `AMDRSHelper.exe`.

### 5.5. Sign

```powershell
cd <repo-root>\tools\signing
.\02_sign_binary.ps1 "..\..\coloruino-loader\Process Hollowing\x64\Release\AMDRSHelper.exe"
```

(Run `01_generate_cert.ps1` once before the first sign.)

### 6. Deploy

In the current single-binary deployment, the app (`pipanel.exe`) is
embedded inside the loader (`AMDRSHelper.exe`) as encrypted bytes - it
never lands on the client's disk separately. The loader handles:
- AES-decrypting the embedded payload at runtime.
- Writing the `data` config file next to itself on first license entry
 (via `data_writer.cpp`, replicating the legacy
 `config-generator` behaviour byte-for-byte).
- Writing `auth.dat` for license persistence.

No DLLs required (the app is /MT).
See [BUILD_GUIDE.md](../../BUILD_GUIDE.md) for the full multi-stage
release pipeline.

## Libraries Linked

| Library | Source | Purpose |
|---------|--------|---------|
| `d3d11.lib` | Windows SDK | Direct3D 11 device, textures |
| `dxgi.lib` | Windows SDK | Desktop Duplication API |
| `d3dcompiler.lib` | Windows SDK | Runtime shader compilation |
| `ws2_32.lib` | Windows SDK | Winsock2 (UDP + TCP) |

## Compiler Settings

### Coloruino

| Setting | Value |
|---------|-------|
| C++ Standard | C++17 |
| Optimization | /O2 (maximize speed) |
| Platform | x64 |
| Runtime Library | /MT (static CRT) recommended |

### ProcessHollowing

| Setting | Value |
|---------|-------|
| C++ Standard | C++17 |
| Optimization | /Os (minimize size) |
| Platform | x64 |
| Runtime Library | /MT (static CRT) |
| Subsystem | Windows (not Console) |
| Entry Point | mainCRTStartup |
| Debug Info | OFF for Release |
| UAC Level | asInvoker (default) |

## Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| E#F3-1 "File Corrupted" | VMProtect Memory Protection ON for coloruino | Turn Memory Protection OFF in VMProtect settings for the coloruino packing step |
| GPU spikes on RX 5500 XT | Old code used AcquireNextFrame(0) with manual frame pacing | Already fixed: uses AcquireNextFrame(1ms) - kernel-blocks until vblank |
| Silent aim overshooting | Distance value too high | Recalibrate `distance` - formula is now `moveX = deltaX * distance` (simplified) |
| Config file not loading | Wrong encryption key or HWID mismatch | Delete `data` + `auth.dat`, relaunch loader, re-enter license (it'll write fresh) |
| Web UI unreachable | Firewall rule not created | Run as admin, or manually add TCP port 13548 inbound rule (display name "AMD Radeon Software Helper") |
