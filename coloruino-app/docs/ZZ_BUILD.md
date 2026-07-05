# Hướng dẫn Build

## Prerequisites

| Component | Version | Purpose |
|-----------|---------|---------|
| Visual Studio | 2022+ | MSVC v143 toolchain |
| Windows SDK | 10.0+ | Win32 API, DXGI, D3D11 |
| VMProtect SDK | 3.x | Chỉ headers (`VMProtectSDK.h`) |
| xorstr | - | Header mã hóa string compile-time |

## Các bước build

### 1. Build Coloruino

1. Mở solution trong Visual Studio
2. Đặt configuration: **Release / x64**
3. Build solution
4. Output: `x64\Release\pipanel.exe` (hoặc output name đã cấu hình)

### 2. VMProtect Pass 1 (Coloruino)

Mở executable đã build trong VMProtect.

| Setting | Value |
|---------|-------|
| Memory Protection | **OFF** |
| Import Protection | ON |
| Resource Protection | ON |
| Packer | ON |
| Strip Relocations | OFF |
| Strip Debug Info | ON |

**Quan trọng:** Memory Protection phải OFF. Nó chạy CRC integrity check với PE file
trên đĩa. Khi binary chạy qua process hollowing, file trên đĩa là process khác -
CRC fail với E#F3-1.

Bảo vệ mọi function có VMProtect marker:
- `generateHtml` (Mutation)
- `handleHttpRequest` (Mutation)

Output: `pipanel.exe` đã pack (VS project TargetName là `pipanel`;
file đã pack overwrite file chưa pack tại chỗ).

### 3. HxD Byte Export

1. Mở `pipanel.exe` đã pack trong HxD.
2. Edit -> Select All -> Edit -> Copy as -> C -> "Unsigned char array".
3. Paste vào `coloruino-loader/Process Hollowing/TabTip32_exe_bytes.h`,
 thay mảng hiện có.
4. Verify: mảng tên `TabTip32_exe`, biến length `TabTip32_exe_len`.
 (Tên file và identifier legacy - `TabTip32_exe_bytes.h`,
 `TabTip32_exe`, `TabTip32_exe_len` - được giữ để tương thích symbol
 với loader code, dù binary bên dưới là `pipanel.exe` và loader là
 `AMDRSHelper.exe`.)

### 3.5. Encrypt Embedded Payload

```
cd "coloruino-loader/Process Hollowing"
python gen_build_secrets.py # produces build_secrets.h (kBuildSalt + kPayloadIV) if missing
python encrypt_payload_aes.py <license-key>
```

AES-256-CBC-encrypt mảng `TabTip32_exe[]` tại chỗ với
key = `SHA256(license || kBuildSalt)`, IV = `kPayloadIV`. Loader
dẫn xuất cùng key ở runtime từ license người dùng nhập.

### 4. Build Loader (coloruino-loader)

1. `TabTip32_exe_bytes.h` đã mã hóa đã nằm trong project từ step 3.
2. Mở `coloruino-loader/Process Hollowing.sln`.
3. Build: **Release / x64**.
4. PostBuildEvent tự chạy `sanitize_pe.py` (Rich header,
 timestamp, debug dir, section names, PE checksum).
5. Output: `x64\Release\AMDRSHelper.exe`.

### 5. VMProtect Pass 2 (ProcessHollowing)

> Tùy chọn trong build hiện tại - homebrew anti-debug + dynamic WinAPI
> resolution + xorstr literals đã cung cấp baseline không cần VMProtect
> trên loader. Nếu BẠN pack loader, làm SAU `sanitize_pe` (`sanitize_pe`
> chạy như PostBuildEvent nên đã xong khi bạn mở binary trong VMProtect).

Mở `AMDRSHelper.exe` trong VMProtect.

| Setting | Value |
|---------|-------|
| Memory Protection | **ON** |
| Import Protection | ON |
| Resource Protection | ON |
| Packer | ON |

Bảo vệ mọi function có VMProtect marker:
- `RP32`, `RP64`, `RPR32`, `RPR64` (Ultra)
- `FRT` (Mutation)
- `MainFunction` (Ultra)

Output: `AMDRSHelper.exe` cuối.

### 5.5. Sign

```powershell
cd <repo-root>\tools\signing
.\02_sign_binary.ps1 "..\..\coloruino-loader\Process Hollowing\x64\Release\AMDRSHelper.exe"
```

(Chạy `01_generate_cert.ps1` một lần trước lần ký đầu.)

### 6. Deploy

Trong triển khai single-binary hiện tại, app (`pipanel.exe`) được nhúng
bên trong loader (`AMDRSHelper.exe`) dưới dạng byte mã hóa - nó không bao giờ
nằm riêng trên đĩa client. Loader xử lý:
- AES-decrypt payload nhúng khi runtime.
- Ghi file config `data` cạnh nó ở lần nhập license đầu tiên
 (qua `data_writer.cpp`, replicate hành vi legacy `config-generator` byte-for-byte).
- Ghi `auth.dat` để persist license.

Không cần DLL (app dùng /MT).
Xem [ZZ_BUILD_GUIDE.md](../../ZZ_BUILD_GUIDE.md) để có pipeline release nhiều stage đầy đủ.

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
| E#F3-1 "File Corrupted" | VMProtect Memory Protection ON cho coloruino | Tắt Memory Protection trong VMProtect settings ở bước pack coloruino |
| GPU spike trên RX 5500 XT | Code cũ dùng AcquireNextFrame(0) với manual frame pacing | Đã fix: dùng AcquireNextFrame(1ms) - kernel-block tới vblank |
| Silent aim overshooting | Giá trị Distance quá cao | Recalibrate `distance` - formula giờ là `moveX = deltaX * distance` (đã đơn giản hóa) |
| Config file không load | Sai encryption key hoặc HWID mismatch | Xóa `data` + `auth.dat`, chạy lại loader, nhập lại license (nó sẽ ghi mới) |
| Web UI không truy cập được | Firewall rule chưa tạo | Chạy as admin, hoặc tự thêm inbound rule TCP port 13548 (display name "AMD Radeon Software Helper") |
