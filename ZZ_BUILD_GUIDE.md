# Coloruino - Hướng dẫn Build

Pipeline kỹ thuật đầy đủ để đi từ source tới binary triển khai.
Đây là tài liệu vận hành - nếu bạn muốn hướng dẫn setup dễ hiểu,
đọc [ZZ_USER_GUIDE.md](ZZ_USER_GUIDE.md).

---

## Điều kiện toolchain

### Máy build

| Tool | Version | Lý do |
|---|---|---|
| Windows 10/11 x64 | bất kỳ build mới | Host OS cho MSVC |
| Visual Studio 2022 | 17.x với C++ Desktop workload | Compile app, loader, config-generator |
| Windows 10 SDK | 10.0.19041+ | Headers, signtool.exe |
| MSVC v143 toolset | đi kèm VS2022 | C++17 với runtime /MT |
| Python | 3.10+ | gen_build_secrets.py, sanitize_pe.py, encrypt_payload_aes.py, rotate_secrets.py |
| PowerShell | 5.1+ (có sẵn) | Script ký |
| VMProtect | 3.5.x với SDK | Pack binary release |
| HxD | bản mới bất kỳ | Dump binary -> C-array cho payload loader |
| Arduino IDE | 1.8.19 hoặc 2.x | Flash firmware vào Leonardo |

### Client PC (triển khai single-PC - máy chạy Valorant)

| Tool | Version | Lý do |
|---|---|---|
| Windows 10/11 x64 | bất kỳ build mới | Chạy loader và Valorant |
| PowerShell | 5.1+ | Shell admin một lần để cài signing root cert |
| Microsoft Visual C++ Redistributable 2015-2022 | x64 | App dùng /MT nên redist không bắt buộc, nhưng có sẽ tránh edge-case |
| AnyDesk (hoặc tương đương) | mới | Inbound - tùy chọn - nếu remote deploy cho người khác, AnyDesk cho phép bạn nhập license mà người nhận không thấy |

Không cần tool persistent sau khi cài. Bạn ship (hoặc tự copy)
`AMDRSHelper.exe` (lâu dài) + `config_generator.exe` (xóa sau setup),
cộng Arduino đã pre-flash như một thiết bị vật lý.

---

## Setup máy build một lần

### 1. Clone repository

```
git clone <repo> C:\Users\<you>\Desktop\coloruino-extra\coloruino
```

Build pipeline giả định path dưới
`C:\Users\<you>\Desktop\coloruino-extra\coloruino`. Nếu đặt nơi khác
hãy chỉnh script - phần lớn dùng `$PSScriptRoot` nên chạy được in-place.

### 2. Patch Arduino AVR core

Firmware của Coloruino spoof một chuột USB của vendor placeholder. Arduino IDE
ship profile Leonardo trỏ tới VID/PID Leonardo thật, dễ fingerprint. Patch tại chỗ:

Mở
`%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\boards.txt`
và thêm một entry board MỚI (đừng ghi đè Leonardo hiện có):

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

Sau đó trong
`%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\cores\arduino\USBDesc.h`:

```c
#define IPRODUCT 1
#define IMANUFACTURER 2
#define ISERIAL 0
```

Và trong
`%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\cores\arduino\USBCore.cpp`,
đảm bảo device descriptor đọc như sau:

```c
// class subclass protocol packetSize0 VID PID bcdDevice
DeviceDescriptor PROGMEM = D_DEVICE(0x00, 0x00, 0x00, 8, USB_VID, USB_PID, 0x100, ...);
```

(class/subclass/protocol đều 0x00, packetSize0 = 8 - đây là giá trị chuột vendor placeholder thật report.)

### 3. Cài VMProtect

Cài VMProtect 3.5.x. Copy `VMProtectSDK64.lib` và `VMProtectSDK64.dll`
vào `coloruino-app/coloruino5500/Source/Windows/` và
`coloruino-loader/Process Hollowing/Source/Windows/` (library path
`Source\Windows` được tham chiếu từ từng `.vcxproj`).

Loader cũng ship stub `VMProtectSDK.h` no-op các marker SDK - loader KHÔNG
được VMProtect-pack trong setup homebrew; marker giữ trong source như comment.
Ứng dụng PC THÌ được VMProtect-pack.

### 4. Tạo signing certificate

```powershell
cd <repo-root>\tools\signing
.\01_generate_cert.ps1
```

Tạo `code_signing.pfx`, `code_signing.cer`, `code_signing.password.txt`
trong `tools/signing/`. `.pfx` và `.password.txt` đã gitignored -
kiểm tra trước khi commit.

---

## Chuỗi build (mỗi release)

Pipeline có dependency: bạn phải build `coloruino-app` trước (binary đã pack
của nó là payload loader), rồi `coloruino-loader`, rồi `coloruino-config-generator`.
Firmware độc lập.

### Stage 1 - Rotate secrets (tùy chọn nhưng khuyến nghị mỗi release)

```
cd <repo-root>
python rotate_secrets.py
```

In giá trị mới cho:
- License key (32 hex chars)
- User + password Basic auth WebUI
- Config XOR key (24 hex / 12 byte)
- License hash key (18 ASCII)
- Protocol XOR key (16 byte)

Cùng hướng dẫn theo từng key về file source cần update. Paste giá trị mới vào các file được liệt kê. Sau stage này, source ở rotation mới.

> `rotate_secrets.py` KHÔNG tự sửa source - nó chỉ in ra.
> Bạn paste thủ công. Đây là chủ đích: bạn có thể chạy bao nhiêu lần cũng được
> để xem các bộ random khác nhau, chỉ commit khi hài lòng.

### Stage 2 - Build coloruino-app

Ứng dụng PC. Visual Studio 2022 -> mở
`coloruino-app/coloruino5500.sln` -> Release | x64.

**Project settings cần kiểm tra** (`coloruino5500.vcxproj`):

| Setting | Value | Lý do |
|---|---|---|
| SubSystem | Windows | Không hiện console window |
| EntryPointSymbol | mainCRTStartup | CRT entry chuẩn gọi `main()` |
| RuntimeLibrary | MultiThreaded (/MT) | Không phụ thuộc CRT DLL khi chạy |
| GenerateDebugInformation | No (Release) | Không ship PDB |
| UACExecutionLevel | (not set) | App không cần elevation |
| LibraryPath | Source\Windows;$(LibraryPath) | Tìm `VMProtectSDK64.lib` |
| TargetName | pipanel | Output là pipanel.exe |

Build. Output nằm ở `coloruino-app/coloruino5500/x64/Release/pipanel.exe`.

### Stage 3 - VMProtect pack ứng dụng PC

Mở VMProtect GUI. Mở `pipanel.exe`. Source có marker VMProtect
(`VMProtectBeginUltra`, `VMProtectBeginMutation`) trong C++ -
VMProtect tự nhận các marker này.

Settings:
- Compilation type: Mutation + Ultra (dùng default)
- Watermark: tùy chọn
- Output: overwrite `pipanel.exe` tại chỗ

Click "Compile". Output: cùng path `pipanel.exe`, giờ đã pack.

### Stage 4 - Convert binary đã pack thành C array

Loader nhúng app binary đã pack dưới dạng literal `static const uint8_t[]`.
Dùng HxD:

1. Mở `pipanel.exe` đã pack trong HxD.
2. `Edit -> Select All`.
3. `Edit -> Copy as -> C` -> "Unsigned char array".
4. Paste kết quả vào
 `coloruino-loader/Process Hollowing/TabTip32_exe_bytes.h`, thay thế
 mảng hiện có.

Cấu trúc cần có của `TabTip32_exe_bytes.h`:

```c
#pragma once
#include "VMProtectSDK.h"

#include <cstdint>

static uint8_t TabTip32_exe[] = {
 0x4D, 0x5A, 0x90, 0x00, /* ... thousands of bytes ... */
};

static const size_t TabTip32_exe_len = sizeof(TabTip32_exe);
```

(`static` chứ không phải `static const` là có chủ đích - loader giải mã
mảng tại chỗ khi chạy.)

### Stage 5 - Tạo per-build secrets

```
cd "coloruino-loader/Process Hollowing"
python gen_build_secrets.py
```

Tạo `build_secrets.h` chứa:
- `kBuildSalt[32]` - 32 byte random thêm vào license khi dẫn xuất AES key
- `kPayloadIV[16]` - IV random cho AES-CBC payload encryption

Chúng chỉ được tạo lại nếu thiếu `build_secrets.h`. Muốn ép mới:
xóa file trước.

> `.vcxproj` có PreBuildEvent chạy việc này tự động nếu header thiếu.
> Thường không cần gọi thủ công.

### Stage 6 - Mã hóa payload nhúng

```
python encrypt_payload_aes.py <license-key>
```

Đọc `TabTip32_exe[]` từ `TabTip32_exe_bytes.h`, dẫn xuất key =
`SHA256(license || kBuildSalt)`, mã hóa bằng AES-256-CBC với
`kPayloadIV`, ghi ciphertext trở lại header - thay plaintext byte tại chỗ.

License key dùng ở đây PHẢI khớp với thứ người dùng nhập vào dialog loader
ở runtime, VÀ phải khớp constant FNV-1a trong `license.cpp:24`.
Nếu đã rotate ở Stage 1, dùng key mới.

> Sau stage này, mảng `TabTip32_exe[]` chứa ciphertext.
> Đừng mở lại trong HxD trừ khi bạn bắt đầu lại.

### Stage 7 - Build coloruino-loader

Mở `coloruino-loader/Process Hollowing.sln`. Release | x64. Build.

**Project settings cần kiểm tra** (`Process Hollowing.vcxproj`):

| Setting | Value | Lý do |
|---|---|---|
| TargetName | AMDRSHelper | Output là AMDRSHelper.exe |
| SubSystem | Windows | Dialog license hiện, không console |
| RuntimeLibrary | MultiThreaded (/MT) | Self-contained |
| LibraryPath | Source\Windows;$(LibraryPath) | Header VMProtect SDK nếu dùng |
| PreBuildEvent | python gen_build_secrets.py | Idempotent |
| PostBuildEvent | python sanitize_pe.py "$(TargetPath)" | Xóa Rich header / timestamp / debug dir, randomize section names, tính lại PE checksum |

Output: `coloruino-loader/Process Hollowing/x64/Release/AMDRSHelper.exe`.

PostBuildEvent chạy `sanitize_pe.py` tự động - bạn sẽ thấy output trong build log.
Nếu có thứ khác hook vào event này, đảm bảo `sanitize_pe.py` chạy CUỐI
(rewrite thứ khác trước, rồi sanitize).

### Stage 8 - Build coloruino-config-generator

Mở project config-generator, build Release | x64. Output:
`config_generator.exe`. Đây là binary console nhỏ nhận license qua
`argv[1]` và ghi file `data` mã hóa.

### Stage 9 - Ký cả hai binary

```powershell
cd <repo-root>\tools\signing

.\02_sign_binary.ps1 "..\..\coloruino-loader\Process Hollowing\x64\Release\AMDRSHelper.exe"
.\02_sign_binary.ps1 "..\..\coloruino-app\coloruino5500\x64\Release\pipanel.exe" -Description "AMD Radeon Settings"
.\02_sign_binary.ps1 "..\..\coloruino-config-generator\<output-path>\config_generator.exe" -Description "AMD Radeon Configuration Tool"
```

Mỗi lần gọi:
- Tự tìm `signtool.exe` từ Windows SDK đã cài.
- Đọc `code_signing.pfx` + `code_signing.password.txt` từ cùng thư mục.
- Ký SHA-256 + cert `AMD Radeon Software`.
- Đặt file description hiển thị trong Properties -> Digital Signatures.

> Vì sao ký SAU VMProtect: VMProtect rewrite binary, sẽ làm mất hiệu lực
> chữ ký nếu ký trước khi pack. Ký cuối.

> Vì sao ký SAU sanitize_pe: sanitize_pe cũng rewrite binary. Ký sau
> stage sanitize của loader. (Với pipanel.exe không có bước sanitize -
> nhưng cùng quy tắc: ký cuối.)

> Vì sao không bake vào `.vcxproj` như PostBuildEvent: VMProtect diễn ra
> ngoài VS build, và build event sẽ chạy TRƯỚC VMProtect. Ký là stage thủ công
> trong quy trình release.

### Stage 10 - Flash firmware

Mở `coloruino-fw/coloruino-fw.ino` trong Arduino IDE.
Tools -> Board -> **Arduino Leonardo (MOD)**. Tools -> Port -> COM đúng.
Click Upload.

Kiểm tra host PC thấy Arduino là "<your placeholder product>" bởi "<your placeholder mfr>"
trong Devices and Printers. Nếu nó hiện "Arduino Leonardo", patch AVR core
chưa ăn - kiểm tra lại Step 2 của setup một lần.

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

## Triển khai lên PC chơi

Setup mặc định chạy trực tiếp trên PC chơi. **Triển khai single-binary** -
chỉ `AMDRSHelper.exe` là permanent trên client. Loader tự ghi cả `auth.dat`
(license cache) và `data` (app config với `LICENSE_HWID` ràng buộc HWID)
khi nhập license thành công lần đầu.

Hướng dẫn dễ hiểu chi tiết nằm ở [ZZ_USER_GUIDE.md](ZZ_USER_GUIDE.md);
đây là reference kỹ thuật.

1. Stage lên PC chơi (thả vào nơi ít gây chú ý -
 `C:\Program Files\AMD\CNext\CNext\` là lựa chọn hợp lý):
 - `AMDRSHelper.exe` - **permanent**
 - `code_signing.cer` - chỉ khi client chưa trust cert
 - `03_install_root_cert.ps1` - tương tự

2. (Một lần cho mỗi client) Cài root cert trong PowerShell admin:
 ```powershell
 cd <staging folder>
 .\03_install_root_cert.ps1
 ```
 Kỳ vọng hai dòng xanh "Imported into ...". Xóa file `.cer` +
 `.ps1` sau đó.

3. Launch:
 ```cmd
 AMDRSHelper.exe
 ```
 Dialog license xuất hiện. **Bạn** paste license, click OK.
 Dialog đóng, cursor giật nhẹ (cheat alive), và loader ghi:
 - `auth.dat` - license cache mã hóa theo HWID (để các lần chạy sau
 không hỏi).
 - `data` - config seed mã hóa XOR với `LICENSE_HWID=<hash>`
 ràng buộc máy này. App dùng default compile-in cho mọi thứ còn lại.

4. Test qua WebUI: mở `http://localhost:13548/` trong bất kỳ trình duyệt
 nào trên PC chơi. Nhấn "Test" - cursor nên giật.

5. Xong. License nằm trong `auth.dat`, mã hóa theo HWID máy này.

> Nếu file `data` đã tồn tại khi loader chạy (ví dụ từ cài đặt trước),
> nó được giữ nguyên - loader chỉ tạo `data` nếu thiếu. Việc này giữ
> mọi tweak client đã chỉnh qua WebUI.

> Khi debug và muốn force-regenerate `data` mà không dùng loader,
> `config_generator.exe` vẫn hoạt động như trước. Build nó giống app,
> chạy thủ công với license làm argument. Không thuộc deployment client
> thông thường.

---

## Triển khai Arduino

Đã làm ở Stage 10 ở trên. Sau khi flash, Arduino chạy firmware mỗi lần cấp nguồn. Trên PC chơi, cắm cả hai:
- Cáp USB: Arduino -> cổng USB client (xuất HID mouse tới OS chạy Valorant)
- Cáp Ethernet: W5500 shield của Arduino -> NIC phụ của client (nhận lệnh UDP từ ứng dụng PC đã hollow)

NIC của PC chơi nối với Arduino phải có IP tĩnh cùng subnet với shield Arduino
(subnet mặc định `192.168.1.0/24`, client chọn ví dụ `192.168.1.10`).

---

## Quy trình rotate

### Chỉ rotate license

1. Generate hoặc chọn license 32-hex mới.
2. Update ba file source (FNV-1a hash trong mỗi file):
 - `coloruino-loader/Process Hollowing/license.cpp:24` (literal trong `ct_fnv1a`)
 - `coloruino-app/coloruino5500/src/security/LicenseManager.cpp:35` (literal trong `ct_fnv1a`)
 - `coloruino-config-generator/config_generator.cpp:29` (`VALID_LICENSE`)
3. Rebuild cả ba binary.
4. Re-encrypt payload:
 ```
 python encrypt_payload_aes.py <new-license>
 ```
5. Rebuild loader (để ciphertext mới được nhúng).
6. Ký lại mọi binary.
7. Trên từng PC chơi: xóa `data` và `auth.dat` cũ, redeploy
 `AMDRSHelper.exe`, chạy loader, nhập license mới. Loader ghi cặp
 `auth.dat` và `data` mới trong một bước.

### Rotate signing identity

Xem `tools/signing/README.md`. Tóm tắt:

1. Xóa `code_signing.pfx`, `code_signing.cer`, `code_signing.password.txt`.
2. Chạy lại `01_generate_cert.ps1`.
3. Ký lại mọi binary (`02_sign_binary.ps1`).
4. Trên PC chơi, uninstall thumbprint cert CŨ khỏi
 `LocalMachine\Root` + `LocalMachine\TrustedPublisher`, chạy lại
 `03_install_root_cert.ps1` với `code_signing.cer` mới.

### Rotate WebUI credentials

Update literal `xorstr_("...")` trong
`coloruino-app/coloruino5500/src/security/Auth.cpp:62-63`.
Rebuild app -> re-VMProtect -> re-embed vào loader -> re-sign.

### Rotate XOR / protocol keys

Xem output `rotate_secrets.py`. Nó in chính xác file + dòng.
Sau khi update, rebuild các component bị ảnh hưởng VÀ re-flash firmware
(nếu đổi protocol key).

---

## Tham chiếu file

### Secret nằm trong source (commit-safe)

- Constant FNV-1a trong `license.cpp`, `LicenseManager.cpp`,
 `config_generator.cpp` - đây là hash, không phải key gốc.
- XOR key trong `ConfigManager.cpp`, `hwid.cpp`, `Auth.cpp`,
 `UDPClient.cpp`, `coloruino-fw.ino` - dù sao cũng thấy trong binary,
 nhưng chỉ giải mã được bằng cách tấn công process đang chạy.

### Secret lúc build (KHÔNG BAO GIỜ commit)

- `tools/signing/code_signing.pfx`
- `tools/signing/code_signing.password.txt`
- `coloruino-loader/Process Hollowing/build_secrets.h` (tái tạo được)
- Bất kỳ bản copy local nào của license key

### Artifact theo máy (KHÔNG BAO GIỜ commit, ship cùng binary)

- `auth.dat` - tạo cạnh `AMDRSHelper.exe` ở lần chạy đầu
- `data` - tạo cạnh `AMDRSHelper.exe` bởi config_generator

Hai file này được mã hóa theo HWID của máy đích. Chuyển sang máy khác
không có tác dụng (decrypt fail âm thầm -> hỏi lại license / chạy lại
config_generator).

### Artifact phân phối được

- `tools/signing/code_signing.cer` (public cert - cần cho `03_install_root_cert.ps1`)
- `tools/signing/03_install_root_cert.ps1`

Bạn có thể publish chúng trên webpage hoặc chia sẻ qua kênh bình thường.

---

## Lỗi thường gặp

### "encrypt_payload_aes.py báo 'kBuildSalt not found'"

`build_secrets.h` chưa tồn tại. Chạy `gen_build_secrets.py` trước
(hoặc build loader một lần - PreBuildEvent sẽ làm).

### "Loader build được nhưng crash khi launch"

Gần như luôn là mismatch payload: key encryption loader derive không khớp
key đã dùng để encrypt. Nguyên nhân thường gặp:
- Bạn rebuild `build_secrets.h` giữa encrypt và build loader ->
 `kBuildSalt` đổi -> derived key mismatch -> decryption tạo rác ->
 process hollowing crash.
 Fix: chạy lại `python encrypt_payload_aes.py <license>`, rồi rebuild
 loader mà không regenerate `build_secrets.h`.
- License bạn dùng để encrypt không khớp constant FNV-1a trong
 `license.cpp`. Fix: rotate về giá trị nhất quán.

### "Loader chạy nhưng ứng dụng không chạy"

Loader chọn process target ngẫu nhiên để hollow; một số target fail.
Retry loop (tối đa 8 lần) xử lý việc này - đảm bảo bạn đang ở build đã patch
có retry loop trong `Process_Hollowing.cpp`. Nếu vẫn fail, payload nhúng
bị hỏng (làm lại Stage 4 và re-encrypt) hoặc VMProtect pack thứ không tương thích
với hollowing (test không VMProtect trước để cô lập).

### "Loader cứ hỏi license mỗi lần chạy"

`auth.dat` không persist. Code đã fix lưu `auth.dat` cạnh exe
(resolve qua `GetModuleFileNameW`), không trong CWD. Rebuild từ source sau fix.
Nếu vẫn xảy ra sau fix: HWID không ổn định (`first_mac` chọn adapter khác mỗi lần) -
xem [ZZ_SECURITY.md](ZZ_SECURITY.md) cho mitigation dự kiến.

### "signtool: SignTool Error: No certificates were found"

File `.pfx` bị xóa hoặc chưa tạo. Chạy lại `01_generate_cert.ps1`.
Nếu đang rotate, cũng dọn cert store trên máy đích trước.

### "SmartScreen vẫn cảnh báo dù tôi đã ký"

Máy đích chưa chạy `03_install_root_cert.ps1`. Self-signed cert không validate
qua chain built-in của Windows - cần cài public cert vào `LocalMachine\Root`
và `LocalMachine\TrustedPublisher`.

### "Defender quarantine binary ngay sau khi ký"

Defender có thể flag theo heuristic (entropy, section đã pack, v.v.)
độc lập với chữ ký. Hoặc thêm folder exclusion trên máy đích, hoặc chấp nhận
trade-off và để nó quarantine build đầu trong khi whitelist qua UI Defender.

### "Firmware compile nhưng Arduino hiện Arduino Leonardo, không phải USB GAMING MOUSE"

Patch board profile (bước setup một lần số 2) chưa áp dụng. Kiểm tra
Tools -> Board bạn đã chọn entry nào. Nếu chỉ thấy "Arduino Leonardo"
và không có "Arduino Leonardo (MOD)", `boards.txt` chưa có entry mới.
Áp dụng lại.

---

## Pre-flight checklist

Trước khi tuyên bố release xong:

- [ ] Đã chạy `rotate_secrets.py`, paste mọi giá trị vào source.
- [ ] App build fresh Release|x64.
- [ ] App đã VMProtect-pack.
- [ ] `pipanel.exe` dump sang `TabTip32_exe_bytes.h` bằng HxD.
- [ ] `gen_build_secrets.py` (hoặc PreBuildEvent) tạo `build_secrets.h`.
- [ ] `encrypt_payload_aes.py <license>` re-encrypt mảng.
- [ ] Loader rebuild fresh Release|x64.
- [ ] `sanitize_pe.py` chạy qua PostBuildEvent (kiểm tra build log).
- [ ] Config-generator rebuild fresh Release|x64.
- [ ] Cả ba binary ký bằng `02_sign_binary.ps1`.
- [ ] Firmware reflashed (chỉ khi rotation gồm protocol key).
- [ ] Trên PC chơi: xóa mọi `auth.dat` và `data` cũ, chạy
 `config_generator.exe`, launch loader, nhập license.
- [ ] WebUI truy cập được từ điện thoại tại `http://<PC1>:13548/`.
- [ ] Test cursor twitch qua nút "Test" của WebUI.
- [ ] Live aim test với enemy trong Custom game.
