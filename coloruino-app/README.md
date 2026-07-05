# coloruino-app (pipanel.exe)

Color-based screen analysis. DXGI Desktop Duplication, 16 MB RGB
lookup-table classifier, per-mode aim/silent/flicker/triggerbot
pipeline, optional GPU compute shader for closest-pixel detection,
WebUI on `:13548`, sends DNS-shaped UDP to an Arduino.

> See also: top-level [README](../README.md), [USER_GUIDE](../USER_GUIDE.md),
> [BUILD_GUIDE](../BUILD_GUIDE.md), [ARCHITECTURE](../ARCHITECTURE.md),
> [SECURITY](../SECURITY.md).

---

## Architecture Overview

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

### Data Flow (post-2026-05-31 refactor)

1. **Screen Capture** - DXGI Desktop Duplication acquires frames from the GPU. Capture region size = MAX of all active modes' FOVs, recomputed each iter (`ComputeMaxFov()`).
2. **Color Detection** - 16MB boolean LUT (256³) classifies target pixels instantly.
3. **FindTargets per-mode** - single linear scan tracks 3 separate candidates (aimbot, silent, flicker), each filtered by its own half-FOV mask inside the scan.
4. **Post-processing per-mode** - per-mode cluster validation; `RefineHeadAnchor` (shoulder-band X + proportional Y) for silent + flicker.
5. **Per-mode globals published** - `apply_delta_x/y`, `mode_a_x/y`, `nonmode_a_x/y`. `capture_seq` bumped to wake silent/flicker threads.
6. **Movement Calculation** - `apply_delta` with distance-aware smoothing; `SnapShoot_P/F` linear gain.
7. **UDP Transmission** - Command sent to Arduino as `<prefix><x>,<y>\r`.
8. **Arduino HID Injection** - Arduino sends USB HID mouse report to OS.

---

## Module Reference

### `src/main.cpp` - Entry Point

**Threads launched:**
| Thread | Function | Purpose |
|--------|----------|---------|
| Capture | `CaptureScreen()` | Main capture + per-mode FindTargets + aimbot/assist/trigger output |
| Mode A | `mode_a()` | Silent aim latch (edge-triggered, 20ms debounce). Reads `mode_a_x/y`. Runs at `THREAD_PRIORITY_HIGHEST`. |
| Non-Mode A | `nonmode_a()` | Flicker latch (edge-triggered, 20ms debounce). Reads `nonmode_a_x/y`. Runs at `THREAD_PRIORITY_HIGHEST`. |
| Web Server | `startWebServer(13548)` | HTTP config UI (spawned inside `initializeNetworking`) |
| Anti-Debug | `AntiDebugThread()` | Continuous debugger detection |

**Startup sequence:**
1. DPI awareness (PER_MONITOR_AWARE_V2)
2. License validation (HWID + FNV-1a hash)
3. Load config from encrypted `data` file
4. Initialize UDP socket to Arduino
5. Push `K` (cooldown) config to Arduino
6. `FreeConsole()` - detach console window
7. Set process to `HIGH_PRIORITY_CLASS` + 0.5ms timer resolution
8. Read screen dimensions via `GetSystemMetrics`
9. `InitFOV()` (sets initial currentFOV - overwritten by capture iter 1)
10. Launch capture / mode_a / nonmode_a threads
11. `initializeNetworking(13548)` (firewall + AntiDebug + WebServer)

### `src/capture/CaptureLoop.cpp` - Main Capture Loop

The core loop runs DXGI-paced (no manual frame timing). `AcquireNextFrame(1ms)` blocks in kernel until a frame is ready, naturally syncing to the display's refresh rate.

**Functions:**

| Function | Description |
|----------|-------------|
| `FindTargets()` | Single linear scan over the MAX-FOV buffer; tracks 3 per-mode `ModeCandidate` structs (aimbot/silent/flicker), each masked by its own half-FOV. |
| `CountNeighbours()` | Checks 8 pixels around a closest-pixel hit. Called PER MODE in `OptimizedProcessImage` for cluster validation. |
| `RefineHeadAnchor()` | (NEW) Tier 1 port from tfirm. Walk-down height + shoulder-band X average + proportional Y offset. Replaces bare topmost-Y for silent + flicker when `mode_a_head_targeting` + `head_anchor_proportional` are both on. |
| `OptimizedProcessImage()` | Per-mode pipeline: FindTargets -> per-mode cluster check -> RefineHeadAnchor (silent/flicker) -> dead body filter (silent only) -> publish per-mode coords. |
| `ProcessGPUResult()` | GPU compute path result handler. Filters the single closest-pixel result per each mode's FOV. |
| `Otrigger_action()` | Triggerbot. Two modes: polygon check (4-ray crossing test, default) or legacy spiral-first-hit. Reuses capture buffer. |
| `ComputeMaxFov()` | Returns max of active modes' FOVs. Called each iter - picks up live cfg changes. |
| `CaptureScreen()` | Main loop. Branches GPU/CPU based on `cfg::use_gpu_processing`. |

**Capture loop structure:**
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
| `Initialize()` | Creates D3D11 device, acquires DXGI output duplication |
| `CaptureRegionAdaptive()` | CPU path: acquires frame, copies FOV region to staging texture, maps to CPU memory |
| `CaptureRegionGPU()` | GPU path: acquires frame, runs compute shader, reads back dx/dy. Limited to 255x255 FOV (8-bit packing) |
| `UploadLUT()` | Uploads 256^3 boolean LUT as 3D texture for GPU compute |
| `InitializeGPUCompute()` | Compiles cs_5_0 compute shader, creates buffers/UAVs |

**GPU compute pixel packing:** `(distance^2 << 16) | (y << 8) | x` - limits FOV to 255 max per axis.

**Double buffer system:** Two BYTE arrays alternate read/write to avoid stalls.

### `src/capture/ColorDetector.cpp` - Color Detection

Class: `FastColorDetector`

**LUT Architecture:**
- 16MB `std::array<bool, 256*256*256>` indexed by `[R*65536 + G*256 + B]`
- Built once at startup, rebuilt when color mode changes
- Integer HSV conversion (no floating point)

**Color modes:**
| Mode | Color | RGB Range | HSV Range |
|------|-------|-----------|-----------|
| 0 | Purple | (70,0,120)-(255,190,255) | (270,38,40)-(310,100,100) |
| 1 | Anti-Purple | (70,110,120)-(255,190,255) | (270,25,40)-(310,100,100) |
| 2 | Yellow | (168,168,0)-(255,255,110) | (55,5,70)-(65,100,100) |
| 3 | Red | (225,45,45)-(255,136,136) | (0,37,88)-(1,80,100) |

When `useIstrigFilter` is enabled, pixels must pass BOTH RGB range AND HSV range checks.

### `src/input/MouseMove.cpp` - Movement Functions

| Function | Command | Behavior |
|----------|---------|----------|
| `apply_delta()` | `M` | Per-frame smoothing with overflow accumulation. `(delta / smooth) * speed + overflow`. Fractional remainders carried to next frame. |
| `Magnet()` | `M` | Toggle-based assist. Same overflow system. Toggled by `assist_apply_deltakey`. |
| `SnapShoot_P()` | `P` | Silent aim. `moveX = deltaX * distance`. One-shot. |
| `SnapShoot_F()` | `F` | Flicker. Same formula as silent aim with `nonmode_a_distance`. |

**Silent aim formula:**
```
moveX = deltaX * distance
moveY = deltaY * distance
```
The old normalize+clamp(10)+multiply was algebraically equivalent (`(dX/dist)*(dist*m) = dX*m` in all cases, including the clamp path where `(dX/10)*(10*m) = dX*m`) but wasted CPU on sqrt/normalize/clamp.

### `src/network/UDPClient.cpp` - UDP Communication

**Command format:** `<prefix><x>,<y>\r`

| Prefix | Meaning | Arduino Action |
|--------|---------|----------------|
| `M` | Move | Move mouse, preserve real buttons |
| `L` | Click | Press+release left button |
| `P` | Silent aim | Deterministic 4-report sequence: move -> press -> release -> snapback (no artificial delay between reports) |
| `F` | Flicker | Move+click, then release (no snapback) |
| `K` | Cooldown | Set P cooldown in milliseconds on Arduino. Format: `K<ms>\r` |

> **Removed in firmware v2**: `D` (split delay). The P command now uses
> a fixed 4-report sequence with no artificial delay between reports.

**Functions:**

| Function | Description |
|----------|-------------|
| `sendCommand(x, y, prefix)` | Sends `<prefix><x>,<y>\r` movement/action command |
| `sendClick()` | Sends `L\r` click command |
| `sendArduinoConfig(cmd, value)` | Sends single-value config command to Arduino (`K` for cooldown). Format: `<cmd><value>\r` |

Socket: non-blocking UDP (`FIONBIO`). Fire-and-forget, no ACK.

### `src/network/WebServer.cpp` - HTTP Configuration UI

Serves a web UI on **port 13548** for live configuration changes.

**Features:**
- COM-based firewall rule creation (INetFwPolicy2, rule name `AMD Radeon Software Helper`) - no `netsh.exe` child process
- Basic auth protection (credentials rotated via `rotate_secrets.py`)
- Route table built with factory functions (`makeIntRoute`, `makeFloatRoute`, `makeBoolRoute`)
- All changes auto-persisted to encrypted config file
- Anti-debug check on every HTTP request
- xorstr_ encrypted strings in HTML

**Key routes** (51 total - see [docs/API.md](docs/API.md) for the full table):
| Route | Action |
|-------|--------|
| `GET /reconnect` | Reconnect UDP socket |
| `GET /testing` | Send test move (-50, 50) |
| `GET /close` | Shutdown application |
| `GET /color?mode=N` | Switch color mode (0-3) |
| `GET /arduino_ip?value=...` | Update Arduino IP, persist to `data`, reconnect socket |
| `GET /arduino_port?value=...` | Update Arduino port, persist to `data`, reconnect socket |
| `GET /apply_delta?active=1` | Enable/disable aimbot |
| `GET /fov?fov=N` | Set aimbot FOV |
| `GET /smooth?smooth=N` | Set aimbot smoothing |
| `GET /apply_delta_dist_smoothing?active=N` | (NEW) Toggle distance-aware aimbot smoothing |
| `GET /apply_delta_near_mult?value=N` | (NEW) Multiplier when target is very close |
| `GET /gpu_mode?active=1` | Toggle GPU processing |
| `GET /mode_a_head_targeting?active=N` | Toggle head targeting |
| `GET /head_anchor_proportional?active=N` | (NEW) Toggle shoulder-band X + proportional Y |
| `GET /head_anchor_close_pct?value=N` | (NEW) Close-target Y offset % |
| `GET /mode_a_cooldown?value=N` | Set cooldown in milliseconds (pushes `K` command to Arduino) |
| `GET /trigger_polygon_check?active=N` | (NEW) Toggle 4-ray crossing test vs legacy spiral |
| `GET /dead_body_filter?active=N` | Toggle dead body filter (suppress aim on ragdoll) |
| `GET /dead_body_threshold?value=N` | Set Y-delta threshold for dead body detection (3-60 px) |
| `GET /min_cluster_size?value=N` | Set minimum purple neighbours to accept detection (0-8, 0=off) |

**WebUI features** (post-2026-05-31 overhaul):
- Mobile-first responsive CSS grid (cards stack to single column on mobile)
- Sticky header with brand + search bar + global actions
- 9-tab horizontal navigation (Aimbot / Silent / Flicker / Trigger / Head Anchor / Filtering / Color / Performance / All)
- Search filter spans all cards across all tabs
- iOS-style toggle switches (44 × 26 px tap targets)
- Custom-styled range sliders with linked number input + live value hint
- Toast snackbar on every save (1.3 s)
- Tab persistence via `localStorage`
- Split-identity disguise: Spotify (WebUI title, favicon, brand label) + AMD (binary metadata, firewall rule name "AMD Radeon Software Helper")

### `src/core/Config.cpp` - Configuration Values

All runtime-adjustable parameters with defaults:

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

**Distance-Aware Aimbot Smoothing (NEW):**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `apply_delta_dist_smoothing` | true | bool | Master enable |
| `apply_delta_near_dist` | 10 | 1-80 | Near-range pixel threshold |
| `apply_delta_mid_dist` | 30 | 5-200 | Mid-range pixel threshold |
| `apply_delta_near_mult` | 0.4 | 0.05-2.0 | Speed mult when very close |
| `apply_delta_mid_mult` | 0.7 | 0.05-2.0 | Speed mult at mid range |

**Head Anchor Refinement (NEW, silent + flicker):**
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
| `trigger_polygon_check` | true | bool | (NEW) 4-ray crossing test vs legacy spiral-first-hit |

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

### `src/core/Globals.h` - Shared State (post-2026-05-31)

| Global | Type | Purpose |
|--------|------|---------|
| `fovMutex` | `std::mutex` | Protects `currentFOV` (capture thread writes, others can read) |
| `currentFOV` | `int` | MAX of active modes' FOVs. **Written ONLY by capture thread.** |
| `apply_delta_x/y` | `int` | Aimbot delta - filtered by `apply_delta_fov/2` inside FindTargets |
| `mode_a_x/y` | `int` | Silent delta - filtered by `mode_a_fov/2` |
| `nonmode_a_x/y` | `int` | **NEW**: Flicker delta - filtered by `nonmode_a_fov/2` |
| `Width/Height` | `int` | Screen resolution (set at startup) |
| `oX/oY` | `int` | Target screen coords (aimbot's; for overlay/debug) |
| `capture_seq` | `atomic<uint64_t>` | Frame sequence; release-stored after globals written |
| `capture_fov_used` | `atomic<int>` | = currentFOV under MAX-FOV arch (debug-only) |

### `src/core/ConfigManager.cpp` - Encrypted Config

XOR encryption with key `<your 24-hex XOR key>`. Config stored in `data` file next to executable.

**File format (plaintext before encryption):**
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
1. CPUID (EBX, EDX, ECX from leaf 0)
2. BIOS SystemManufacturer + SystemProductName (registry)
3. First network adapter MAC address
4. Windows InstallDate (registry)
5. Salt: `<your 24-hex XOR key>`

**Hash:** 3-round XOR with key `<your ASCII hash key>`, then hex-encoded.

**License validation:** FNV-1a 64-bit compile-time hash comparison.

### `src/security/AntiDebug.cpp` - Anti-Debug

Checks (run continuously in background thread):
- PEB `BeingDebugged` flag
- `NtQueryInformationProcess` with `ProcessDebugPort` (0x7)
- `NtQueryInformationProcess` with `ProcessDebugObjectHandle` (0x1E)
- `CheckRemoteDebuggerPresent`
- Hardware breakpoint registers (DR0-DR3)
- Heap flags
- RDTSC timing check

Detection triggers `KillSelf()` - terminates process.

---

## Build

### Requirements
- Visual Studio 2022+ (MSVC v143)
- Windows SDK 10.0+
- DirectX 11 SDK (included in Windows SDK)
- VMProtect SDK headers (`VMProtectSDK.h`) - compile-time markers only, no DLL needed

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

**Critical:** Memory Protection must be OFF for coloruino's VMProtect pass. It performs CRC checks against the on-disk file, but the hollowed process runs from memory with a different file on disk - causing E#F3-1 "File Corrupted" errors.

---

## Config File Generation

In the current single-binary client deployment, the encrypted `data`
file is written by the loader (`coloruino-loader/.../data_writer.cpp`)
on the client's first successful license entry. Format is identical to
what `config-generator` produces.

The standalone `config-generator` tool still exists (see
[../coloruino-config-generator/README.md](../coloruino-config-generator/README.md))
and can be used supplier-side to force-create a `data` file without
running the loader's prompt - useful for debugging.
