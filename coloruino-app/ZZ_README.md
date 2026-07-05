# coloruino-app (pipanel.exe)

Phân tích màn hình dựa trên màu. DXGI Desktop Duplication, classifier RGB
lookup-table 16 MB, pipeline aim/silent/flicker/triggerbot theo từng mode,
compute shader GPU tùy chọn để tìm closest-pixel, WebUI trên `:13548`,
gửi UDP dạng DNS tới Arduino.

> Xem thêm: [README](../ZZ_README.md) cấp root, [USER_GUIDE](../ZZ_USER_GUIDE.md),
> [BUILD_GUIDE](../ZZ_BUILD_GUIDE.md), [ARCHITECTURE](../ZZ_ARCHITECTURE.md),
> [SECURITY](../ZZ_SECURITY.md).

---

## Tổng quan kiến trúc

```
+----------------------+   UDP    +-----------------+   USB HID   +----+
| coloruino-app        |   M/P/   | Arduino +       |   reports   | OS |
| (pipanel.exe)        |   F/L    | USB Host +      |   -------->  +----+
|                      |  ------> | W5500 stack     |
| DXGI capture         |          | (coloruino-fw)  |
| MAX-FOV per-mode     |          +-----------------+
| filter pipeline      |
| WebUI on :13548      |
+----------------------+
```

### Luồng dữ liệu (sau refactor 2026-05-31)

1. **Screen Capture** - DXGI Desktop Duplication lấy frame từ GPU. Kích thước vùng capture = MAX của FOV mọi mode active, tính lại mỗi iter (`ComputeMaxFov()`).
2. **Color Detection** - LUT boolean 16MB (256^3) classify target pixel tức thì.
3. **FindTargets per-mode** - một linear scan duy nhất track 3 candidate riêng (aimbot, silent, flicker), mỗi cái được filter bằng half-FOV mask riêng trong scan.
4. **Post-processing per-mode** - cluster validation theo mode; `RefineHeadAnchor` (shoulder-band X + proportional Y) cho silent + flicker.
5. **Publish global theo mode** - `apply_delta_x/y`, `mode_a_x/y`, `nonmode_a_x/y`. Tăng `capture_seq` để đánh thức thread silent/flicker.
6. **Movement Calculation** - `apply_delta` với smoothing theo distance; `SnapShoot_P/F` linear gain.
7. **UDP Transmission** - Command gửi tới Arduino dạng `<prefix><x>,<y>\r`.
8. **Arduino HID Injection** - Arduino gửi USB HID mouse report tới OS.

---

## Module Reference

### `src/main.cpp` - Entry Point

**Threads được launch:**
| Thread | Function | Mục đích |
|--------|----------|----------|
| Capture | `CaptureScreen()` | Capture chính + FindTargets theo mode + output aimbot/assist/trigger |
| Mode A | `mode_a()` | Latch silent aim (edge-triggered, debounce 20ms). Đọc `mode_a_x/y`. Chạy `THREAD_PRIORITY_HIGHEST`. |
| Non-Mode A | `nonmode_a()` | Latch flicker (edge-triggered, debounce 20ms). Đọc `nonmode_a_x/y`. Chạy `THREAD_PRIORITY_HIGHEST`. |
| Web Server | `startWebServer(13548)` | HTTP config UI (spawn trong `initializeNetworking`) |
| Anti-Debug | `AntiDebugThread()` | Detect debugger liên tục |

**Startup sequence:**
1. DPI awareness (PER_MONITOR_AWARE_V2)
2. Validate license (HWID + FNV-1a hash)
3. Load config từ file `data` mã hóa
4. Initialize UDP socket tới Arduino
5. Push config `K` (cooldown) tới Arduino
6. `FreeConsole()` - detach console window
7. Set process `HIGH_PRIORITY_CLASS` + timer resolution 0.5ms
8. Đọc screen dimensions qua `GetSystemMetrics`
9. `InitFOV()` (đặt `currentFOV` ban đầu - bị capture iter 1 overwrite)
10. Launch capture / mode_a / nonmode_a threads
11. `initializeNetworking(13548)` (firewall + AntiDebug + WebServer)

### `src/capture/CaptureLoop.cpp` - Main Capture Loop

Core loop chạy theo pacing DXGI (không manual frame timing). `AcquireNextFrame(1ms)`
block trong kernel tới khi frame sẵn sàng, tự nhiên sync với refresh rate của display.

**Functions:**

| Function | Description |
|----------|-------------|
| `FindTargets()` | Một linear scan qua buffer MAX-FOV; track 3 struct `ModeCandidate` theo mode (aimbot/silent/flicker), mỗi cái mask bằng half-FOV riêng. |
| `CountNeighbours()` | Kiểm tra 8 pixel quanh closest-pixel hit. Gọi THEO MODE trong `OptimizedProcessImage` để cluster validation. |
| `RefineHeadAnchor()` | (MỚI) Port Tier 1 từ tfirm. Walk-down height + shoulder-band X average + proportional Y offset. Thay bare topmost-Y cho silent + flicker khi `mode_a_head_targeting` + `head_anchor_proportional` đều bật. |
| `OptimizedProcessImage()` | Pipeline theo mode: FindTargets -> cluster check theo mode -> RefineHeadAnchor (silent/flicker) -> dead body filter (silent only) -> publish coords theo mode. |
| `ProcessGPUResult()` | Handler kết quả GPU compute. Filter single closest-pixel result theo FOV từng mode. |
| `Otrigger_action()` | Triggerbot. Hai mode: polygon check (4-ray crossing test, default) hoặc legacy spiral-first-hit. Tái sử dụng capture buffer. |
| `ComputeMaxFov()` | Trả max FOV của các mode active. Gọi mỗi iter - nhận live cfg change. |
| `CaptureScreen()` | Main loop. Rẽ GPU/CPU theo `cfg::use_gpu_processing`. |

**Cấu trúc capture loop:**
```
while (true) {
 w = ComputeMaxFov() // max of active modes
 if GPU mode:
 try CaptureRegionGPU(timeout=1ms)
 fallback to CaptureRegionAdaptive if GPU fails or FOV > 255
 else:
 CaptureRegionAdaptive(timeout=1ms)

 if captured:
 OptimizedProcessImage() // FindTargets -> 3 sets of per-mode coords
 publish capture_fov_used + capture_seq.fetch_add(1, release)

 apply_delta(apply_delta_x, apply_delta_y, smooth) // aimbot
 Magnet(apply_delta_x, apply_delta_y, smooth) // assist (reads aimbot's)
 Otrigger_action(screenData, capW, capH) // triggerbot
}
```

### `src/capture/ScreenCapture.h/.cpp` - DXGI Capture Engine

Class: `UltraOptimizedDXGICapture`

| Method | Description |
|--------|-------------|
| `Initialize()` | Tạo D3D11 device, lấy DXGI output duplication |
| `CaptureRegionAdaptive()` | CPU path: lấy frame, copy vùng FOV vào staging texture, map sang CPU memory |
| `CaptureRegionGPU()` | GPU path: lấy frame, chạy compute shader, read back dx/dy. Giới hạn FOV 255x255 (8-bit packing) |
| `UploadLUT()` | Upload LUT boolean 256^3 làm 3D texture cho GPU compute |
| `InitializeGPUCompute()` | Compile compute shader cs_5_0, tạo buffer/UAV |

**GPU compute pixel packing:** `(distance^2 << 16) | (y << 8) | x` - giới hạn FOV tối đa 255 mỗi trục.

**Double buffer system:** Hai BYTE array luân phiên read/write để tránh stall.

### `src/capture/ColorDetector.cpp` - Color Detection

Class: `FastColorDetector`

**LUT Architecture:**
- `std::array<bool, 256*256*256>` 16MB index bằng `[R*65536 + G*256 + B]`
- Build một lần lúc startup, rebuild khi color mode đổi
- Chuyển đổi HSV integer (không floating point)

**Color modes:**
| Mode | Color | RGB Range | HSV Range |
|------|-------|-----------|-----------|
| 0 | Purple | (70,0,120)-(255,190,255) | (270,38,40)-(310,100,100) |
| 1 | Anti-Purple | (70,110,120)-(255,190,255) | (270,25,40)-(310,100,100) |
| 2 | Yellow | (168,168,0)-(255,255,110) | (55,5,70)-(65,100,100) |
| 3 | Red | (225,45,45)-(255,136,136) | (0,37,88)-(1,80,100) |

Khi `useIstrigFilter` bật, pixel phải pass CẢ RGB range VÀ HSV range.

### `src/input/MouseMove.cpp` - Movement Functions

| Function | Command | Hành vi |
|----------|---------|---------|
| `apply_delta()` | `M` | Smoothing từng frame với overflow accumulation. `(delta / smooth) * speed + overflow`. Remainder fractional chuyển sang frame sau. |
| `Magnet()` | `M` | Assist dạng toggle. Cùng overflow system. Toggle bằng `assist_apply_deltakey`. |
| `SnapShoot_P()` | `P` | Silent aim. `moveX = deltaX * distance`. One-shot. |
| `SnapShoot_F()` | `F` | Flicker. Cùng formula với silent aim nhưng dùng `nonmode_a_distance`. |

**Silent aim formula:**
```
moveX = deltaX * distance
moveY = deltaY * distance
```
Code normalize+clamp(10)+multiply cũ tương đương đại số (`(dX/dist)*(dist*m) = dX*m` trong mọi trường hợp, kể cả clamp path `(dX/10)*(10*m) = dX*m`) nhưng tốn CPU cho sqrt/normalize/clamp.

### `src/network/UDPClient.cpp` - UDP Communication

**Command format:** `<prefix><x>,<y>\r`

| Prefix | Meaning | Arduino Action |
|--------|---------|----------------|
| `M` | Move | Di chuyển chuột, giữ nút thật |
| `L` | Click | Press+release left button |
| `P` | Silent aim | Chuỗi 4 report deterministic: move -> press -> release -> snapback (không artificial delay giữa report) |
| `F` | Flicker | Move+click, rồi release (không snapback) |
| `K` | Cooldown | Đặt P cooldown theo ms trên Arduino. Format: `K<ms>\r` |

> **Đã bỏ trong firmware v2**: `D` (split delay). Lệnh P giờ dùng chuỗi 4-report cố định,
> không có artificial delay giữa report.

**Functions:**

| Function | Description |
|----------|-------------|
| `sendCommand(x, y, prefix)` | Gửi command movement/action `<prefix><x>,<y>\r` |
| `sendClick()` | Gửi lệnh click `L\r` |
| `sendArduinoConfig(cmd, value)` | Gửi config một giá trị tới Arduino (`K` cho cooldown). Format: `<cmd><value>\r` |

Socket: UDP non-blocking (`FIONBIO`). Fire-and-forget, không ACK.

### `src/network/WebServer.cpp` - HTTP Configuration UI

Serve web UI trên **port 13548** để thay đổi cấu hình trực tiếp.

**Features:**
- Tạo firewall rule bằng COM (INetFwPolicy2, rule name `AMD Radeon Software Helper`) - không spawn `netsh.exe`
- Basic auth protection (credentials rotate qua `rotate_secrets.py`)
- Route table build bằng factory function (`makeIntRoute`, `makeFloatRoute`, `makeBoolRoute`)
- Mọi thay đổi auto-persist vào file config mã hóa
- Anti-debug check trên mọi HTTP request
- String HTML mã hóa bằng xorstr_

**Key routes** (tổng 51 - xem [docs/ZZ_API.md](docs/ZZ_API.md) cho bảng đầy đủ):
| Route | Action |
|-------|--------|
| `GET /reconnect` | Reconnect UDP socket |
| `GET /testing` | Gửi test move (-50, 50) |
| `GET /close` | Shutdown application |
| `GET /color?mode=N` | Đổi color mode (0-3) |
| `GET /arduino_ip?value=...` | Update Arduino IP, persist vào `data`, reconnect socket |
| `GET /arduino_port?value=...` | Update Arduino port, persist vào `data`, reconnect socket |
| `GET /apply_delta?active=1` | Enable/disable aimbot |
| `GET /fov?fov=N` | Set aimbot FOV |
| `GET /smooth?smooth=N` | Set aimbot smoothing |
| `GET /apply_delta_dist_smoothing?active=N` | (MỚI) Toggle distance-aware aimbot smoothing |
| `GET /apply_delta_near_mult?value=N` | (MỚI) Multiplier khi target rất gần |
| `GET /gpu_mode?active=1` | Toggle GPU processing |
| `GET /mode_a_head_targeting?active=N` | Toggle head targeting |
| `GET /head_anchor_proportional?active=N` | (MỚI) Toggle shoulder-band X + proportional Y |
| `GET /head_anchor_close_pct?value=N` | (MỚI) Close-target Y offset % |
| `GET /mode_a_cooldown?value=N` | Set cooldown theo ms (push lệnh `K` tới Arduino) |
| `GET /trigger_polygon_check?active=N` | (MỚI) Toggle 4-ray crossing test vs legacy spiral |
| `GET /dead_body_filter?active=N` | Toggle dead body filter (suppress aim on ragdoll) |
| `GET /dead_body_threshold?value=N` | Set Y-delta threshold cho dead body detection (3-60 px) |
| `GET /min_cluster_size?value=N` | Set số neighbor tím tối thiểu để accept detection (0-8, 0=off) |

**WebUI features** (sau overhaul 2026-05-31):
- CSS grid responsive mobile-first (card stack thành single column trên mobile)
- Sticky header với brand + search bar + global actions
- Navigation ngang 9-tab (Aimbot / Silent / Flicker / Trigger / Head Anchor / Filtering / Color / Performance / All)
- Search filter chạy trên mọi card ở mọi tab
- Toggle switch kiểu iOS (tap target 44 x 26 px)
- Range slider custom với number input liên kết + live value hint
- Toast snackbar mỗi lần save (1.3 s)
- Tab persistence qua `localStorage`
- Disguise tách danh tính: Spotify (title WebUI, favicon, brand label) + AMD (binary metadata, firewall rule name "AMD Radeon Software Helper")

### `src/core/Config.cpp` - Giá trị cấu hình

Mọi tham số runtime-adjustable với default:

**Aimbot (apply_delta):**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `apply_delta_ativo` | true | bool | Enable aimbot |
| `apply_deltakey1` | VK_XBUTTON1 (5) | 0-255 | Primary activation key |
| `apply_deltakey2` | VK_SHIFT (16) | 0-255 | Secondary activation key |
| `target_offset_x` | 1 | 0-20 | Horizontal aim offset |
| `target_offset_y` | 5 | 0-20 | Vertical aim offset (head height) |
| `apply_delta_fov` | 82 | 1-200 | Field of view (pixels) |
| `apply_delta_smooth` | 1.4 | 1.0-4.0 | Smoothing divisor |
| `speed` | 0.4 | 0.1-1.5 | Speed multiplier |

**Silent Aim (mode_a):**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `mode_a_ativo` | true | bool | Enable silent aim |
| `mode_a_key` | VK_XBUTTON2 (6) | 0-255 | Activation key |
| `mode_a_target_offset_x` | 0 | -100 to 100 | Horizontal offset |
| `mode_a_target_offset_y` | 3 | -100 to 100 | Vertical offset |
| `mode_a_fov` | 100 | 1-200 | Field of view |
| `distance` | 2.62 | 0.001-10.0 | Distance multiplier |
| `mode_a_head_targeting` | true | bool | Use topmost-Y anchor (vs closest-to-centre) |
| `mode_a_cooldown_ms` | 50 | 50-500 | Arduino P command cooldown in milliseconds |

**Distance-Aware Aimbot Smoothing (MỚI):**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `apply_delta_dist_smoothing` | true | bool | Master enable |
| `apply_delta_near_dist` | 10 | 1-80 | Near-range pixel threshold |
| `apply_delta_mid_dist` | 30 | 5-200 | Mid-range pixel threshold |
| `apply_delta_near_mult` | 0.4 | 0.05-2.0 | Speed mult when very close |
| `apply_delta_mid_mult` | 0.7 | 0.05-2.0 | Speed mult at mid range |

**Head Anchor Refinement (MỚI, silent + flicker):**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `head_anchor_proportional` | true | bool | Master enable: shoulder-band X + proportional Y |
| `head_anchor_band_rows` | 0 (auto) | 0-20 | Top-band rows averaged (0 = clamp(h/4, 2, 6)) |
| `head_anchor_gap_tolerance` | 2 | 0-10 | Non-purple rows allowed in walk-down |
| `head_anchor_close_pct` | 18 | 0-50 | Y offset % for close targets |
| `head_anchor_mid_pct` | 10 | 0-50 | Y offset % for mid targets |
| `head_anchor_close_min_h` | 30 | 5-200 | Min height (px) for "close" |
| `head_anchor_mid_min_h` | 10 | 1-100 | Min height (px) for "mid" |

**Triggerbot:**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `trigger_action_ativo` | true | bool | Enable triggerbot |
| `trigger_action_key` | VK_MENU (18) | 0-255 | Activation key |
| `trigger_action_fovX` | 3 | 1-20 | Horizontal scan area |
| `trigger_action_fovY` | 3 | 1-20 | Vertical scan area |
| `trigger_polygon_check` | true | bool | (MỚI) 4-ray crossing test vs legacy spiral-first-hit |

**Assist (Magnet):**
| Parameter | Default | Description |
|-----------|---------|-------------|
| `apply_deltaassist_ativo` | false | Enable assist |
| `assist_apply_deltakey` | VK_MENU (18) | Toggle key |
| `apply_deltaassist_fov` | 1 | Field of view |
| `apply_deltaassist_smooth` | 1.5 | Smoothing |
| `assist_speed` | 1.0 | Speed multiplier |

**Flicker (nonmode_a):**
| Parameter | Default | Description |
|-----------|---------|-------------|
| `nonmode_a_ativo` | false | Enable flicker |
| `nonmode_a_key` | VK_XBUTTON2 (6) | Activation key |
| `nonmode_a_fov` | 100 | Field of view |
| `nonmode_a_distance` | 2.5 | Distance multiplier |

**Filtering:**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `dead_body_filter` | false | bool | Suppress silent aim when Y jumps (corpse ragdoll). Opt-in. |
| `dead_body_threshold` | 15 | 3-60 | Y-delta threshold in pixels |
| `min_cluster_size` | 2 | 0-8 | Minimum purple neighbours per mode (0=off) |

### `src/core/Globals.h` - Shared State (sau 2026-05-31)

| Global | Type | Mục đích |
|--------|------|----------|
| `fovMutex` | `std::mutex` | Bảo vệ `currentFOV` (capture thread ghi, bên khác có thể đọc) |
| `currentFOV` | `int` | MAX FOV của mode active. **CHỈ capture thread ghi.** |
| `apply_delta_x/y` | `int` | Delta aimbot - filter bằng `apply_delta_fov/2` trong FindTargets |
| `mode_a_x/y` | `int` | Delta silent - filter bằng `mode_a_fov/2` |
| `nonmode_a_x/y` | `int` | **MỚI**: Delta flicker - filter bằng `nonmode_a_fov/2` |
| `Width/Height` | `int` | Screen resolution (set lúc startup) |
| `oX/oY` | `int` | Target screen coords (của aimbot; dùng overlay/debug) |
| `capture_seq` | `atomic<uint64_t>` | Frame sequence; release-stored sau khi globals được ghi |
| `capture_fov_used` | `atomic<int>` | = currentFOV dưới kiến trúc MAX-FOV (debug-only) |

### `src/core/ConfigManager.cpp` - Encrypted Config

XOR encryption với key `<your 24-hex XOR key>`. Config lưu trong file `data` cạnh executable.

**File format (plaintext trước encryption):**
```
<arduino_ip>
<arduino_port>
LICENSE_HWID=<hashed_hwid>
---CONFIG_START---
key=value
key=value
...
```

### `src/security/LicenseManager.cpp` - HWID + License

**HWID components:**
1. CPUID (EBX, EDX, ECX từ leaf 0)
2. BIOS SystemManufacturer + SystemProductName (registry)
3. MAC address của network adapter đầu tiên
4. Windows InstallDate (registry)
5. Salt: `<your 24-hex XOR key>`

**Hash:** XOR 3 vòng với key `<your ASCII hash key>`, rồi hex-encoded.

**License validation:** So sánh compile-time hash FNV-1a 64-bit.

### `src/security/AntiDebug.cpp` - Anti-Debug

Checks (chạy liên tục trong background thread):
- PEB `BeingDebugged` flag
- `NtQueryInformationProcess` với `ProcessDebugPort` (0x7)
- `NtQueryInformationProcess` với `ProcessDebugObjectHandle` (0x1E)
- `CheckRemoteDebuggerPresent`
- Hardware breakpoint registers (DR0-DR3)
- Heap flags
- RDTSC timing check

Detect thì gọi `KillSelf()` - terminate process.

---

## Build

### Requirements
- Visual Studio 2022+ (MSVC v143)
- Windows SDK 10.0+
- DirectX 11 SDK (đã gồm trong Windows SDK)
- VMProtect SDK headers (`VMProtectSDK.h`) - compile-time markers only, không cần DLL

### Dependencies (linked)
- `d3d11.lib` - Direct3D 11
- `ws2_32.lib` - Winsock2
- `dxgi.lib` - DXGI
- `d3dcompiler.lib` - Shader compilation

### Build Configuration
- Platform: x64
- Configuration: Release
- C++ Standard: C++17
- Optimization: /O2

---

## Protection Pipeline

```
coloruino.exe (x64 Release build)
 |
 v
VMProtect (pack coloruino - Memory Protection OFF, Import Protection ON)
 |
 v
HxD (dump packed binary to C header as byte array)
 |
 v
ProcessHollowing (embed byte array, build)
 |
 v
VMProtect (pack ProcessHollowing - Memory Protection ON, Import Protection ON)
 |
 v
Final AMDRSHelper.exe (signed via tools/signing/02_sign_binary.ps1)
```

**Quan trọng:** Memory Protection phải OFF cho pass VMProtect của coloruino.
Nó chạy CRC check với file on-disk, nhưng hollowed process chạy từ memory với file
trên đĩa khác - gây lỗi E#F3-1 "File Corrupted".

---

## Config File Generation

Trong triển khai client single-binary hiện tại, file `data` mã hóa được loader
(`coloruino-loader/.../data_writer.cpp`) ghi ở lần nhập license thành công đầu tiên
trên client. Format giống hệt thứ `config-generator` tạo.

Tool `config-generator` độc lập vẫn tồn tại (xem
[../coloruino-config-generator/README.md](../coloruino-config-generator/README.md))
và có thể dùng phía supplier để force-create file `data` mà không chạy prompt loader -
hữu ích cho debug.
