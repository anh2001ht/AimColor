# Coloruino API Reference

## Web Server HTTP API

All endpoints accept GET requests. The web UI is served on **port 13548**.
51 routes total (post Phase 6 update).

Auth: HTTP Basic. Credentials baked into the binary, rotated via
`rotate_secrets.py`. Firewall rule auto-created with display name
`AMD Radeon Software Helper`.

### System Controls

| Endpoint | Description | Response |
|----------|-------------|----------|
| `GET /` | Serve configuration web UI (mobile-responsive) | HTML page |
| `GET /reconnect` | Reconnect UDP socket to Arduino | `200 OK` or `500` |
| `GET /testing` | Send test movement (-50, 50) | `200 OK` |
| `GET /close` | Shutdown application | `200 Closing` then exit |
| `GET /arduino_ip?value=X.X.X.X` | Update Arduino IP, persist to `data`, reconnect | `200 OK` |
| `GET /arduino_port?value=N` | Update Arduino port (1-65535), persist, reconnect | `200 OK` |

### Aimbot (apply_delta)

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /apply_delta` | `active` | bool (0/1) | - | Enable/disable |
| `GET /fov` | `fov` | int | 1-200 | Aimbot FOV - contributes to MAX-FOV capture region |
| `GET /smooth` | `smooth` | float | 1.0-4.0 | Smoothing divisor |
| `GET /speed` | `speed` | float | 0.1-1.5 | Speed multiplier |
| `GET /sleep` | `sleep` | int | 0-100 | Sleep between frames (ms) |
| `GET /apply_deltakey1` | `key` | int | 0-255 | Primary key (VK code) |
| `GET /apply_deltakey2` | `key` | int | 0-255 | Secondary key (VK code) |
| `GET /target_offset_x` | `value` | int | -50 to 50 | Horizontal aim offset |
| `GET /target_offset_y` | `value` | int | -20 to 100 | Vertical offset (0=crown, 5=eyes, 25=neck, 50=chest) |

### Distance-Aware Aimbot Smoothing (NEW 2026-05-31)

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /apply_delta_dist_smoothing` | `active` | bool | - | Master enable for distance-aware scaling |
| `GET /apply_delta_near_dist` | `value` | int | 1-80 | Near distance threshold (px) |
| `GET /apply_delta_mid_dist` | `value` | int | 5-200 | Mid distance threshold (px) |
| `GET /apply_delta_near_mult` | `value` | float | 0.05-2.0 | Multiplier when `|delta|` < near_dist |
| `GET /apply_delta_mid_mult` | `value` | float | 0.05-2.0 | Multiplier when `|delta|` < mid_dist |

### Silent Aim (mode_a)

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /mode_a` | `active` | bool | - | Enable/disable |
| `GET /mode_a_fov` | `fov` | int | 1-200 | Silent FOV - contributes to MAX-FOV |
| `GET /mode_a_delay` | `delay` | int | 0-10 | Reserved (unused - cooldown on FW) |
| `GET /mode_a_distance` | `distance` | float | 0.001-10.0 | Pixel -> HID-unit gain multiplier |
| `GET /mode_a_target_offset_x` | `value` | int | -100 to 100 | Horizontal offset (applied on top of head anchor) |
| `GET /mode_a_target_offset_y` | `value` | int | -100 to 100 | Vertical offset (applied on top of head anchor) |
| `GET /mode_a_key` | `key` | int | 0-255 | Activation key |
| `GET /mode_a_head_targeting` | `active` | bool | - | Topmost-Y anchor (vs closest-to-center) |
| `GET /mode_a_cooldown` | `value` | int | 50-500 | P cooldown ms - pushes `K` cmd to Arduino |

### Head Anchor Refinement (silent + flicker, NEW 2026-05-31)

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /head_anchor_proportional` | `active` | bool | - | Master enable: shoulder-band X + proportional Y |
| `GET /head_anchor_band_rows` | `value` | int | 0-20 | Rows averaged for shoulder-band X (0 = auto: h/4 clamped 2..6) |
| `GET /head_anchor_gap_tolerance` | `value` | int | 0-10 | Non-purple rows allowed in walk-down before stopping |
| `GET /head_anchor_close_pct` | `value` | int | 0-50 | Y offset % of measured height for close targets |
| `GET /head_anchor_mid_pct` | `value` | int | 0-50 | Y offset % for mid-range targets |
| `GET /head_anchor_close_min_h` | `value` | int | 5-200 | Min height (px) to be considered "close" |
| `GET /head_anchor_mid_min_h` | `value` | int | 1-100 | Min height (px) to be considered "mid" |

### Flicker (nonmode_a)

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /nonmode_a` | `active` | bool | - | Enable/disable |
| `GET /nonmode_a_key` | `key` | int | 0-255 | Activation key |
| `GET /nonmode_a_fov` | `fov` | int | 1-200 | Flicker FOV - contributes to MAX-FOV |
| `GET /nonmode_a_delay` | `delay` | int | 1-50 | Delay between shots |
| `GET /nonmode_a_distance` | `distance` | float | 0.1-10.0 | Pixel -> HID-unit gain |

### Triggerbot

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /trigger_action` | `active` | bool | - | Enable/disable |
| `GET /trigkey` | `key` | int | 0-255 | Activation key |
| `GET /trigger_action_fovX` | `fovX` | int | 1-20 | Half-width (scan = 2×fovX) |
| `GET /trigger_action_fovY` | `fovY` | int | 1-20 | Half-height (scan = 2×fovY) |
| `GET /trigger_polygon_check` | `active` | bool | - | **NEW**: 4-ray crossing test (vs legacy spiral-first-hit) |

### Color & Performance

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /color` | `mode` | int | 0-3 | 0=Purple, 1=Anti-Purple, 2=Yellow, 3=Red |
| `GET /useIstrigFilter` | `active` | bool | - | Enable HSV+RGB dual filter |
| `GET /gpu_mode` | `active` | bool | - | GPU compute shader path (255×255 max FOV) |

### Filtering

| Endpoint | Parameter | Type | Range | Description |
|----------|-----------|------|-------|-------------|
| `GET /dead_body_filter` | `active` | bool | - | Suppress silent on Y-jump (ragdoll) |
| `GET /dead_body_threshold` | `value` | int | 3-60 | Y-delta threshold (px) |
| `GET /min_cluster_size` | `value` | int | 0-8 | Min purple neighbours per mode (0=off) |

---

## UDP Command Protocol

### Wire Format

```
<prefix><x>,<y>\r
```

- `prefix`: single ASCII character
- `x`, `y`: signed integers (decimal)
- Terminator: carriage return `\r` (0x0D)
- Multiple commands per packet: semicolon-separated (`M10,5;L;M-10,-5`)

### Command Table

| Prefix | Name | X,Y Meaning | Arduino Action |
|--------|------|-------------|----------------|
| `M` | Move | Relative mouse delta | `report(real, x, y)` |
| `L` | Click | Ignored | `report(real\|LEFT, 0, 0)` then `report(real, 0, 0)` |
| `P` | Silent Aim | Movement offset | Deterministic 4-report sequence: move -> press -> release -> snapback. No artificial delay. `K` cooldown applies. |
| `F` | Flicker | Movement offset | `report(real\|LEFT, x, y)` then `report(real, 0, 0)` - no snapback |
| `K` | Cooldown | Single int value (ms) | Set `P_COOLDOWN` on Arduino. Format: `K<value>\r` |

> **Removed in firmware v2**: `D` (split delay). The split-delay model
> was replaced with the 4-report deterministic sequence.

### Socket Configuration

| Property | Value |
|----------|-------|
| Protocol | UDP (SOCK_DGRAM) |
| Mode | Non-blocking (FIONBIO) |
| Default IP | 192.168.1.12 |
| Default Port | 2017 |

---

## Internal C++ API

### CaptureLoop (capture/CaptureLoop.h)

```cpp
void CaptureScreen(); // Main loop - call from dedicated thread, never returns
```

Internal helpers (file-static, not exported):

```cpp
struct ModeCandidate { /* per-mode tracking for FindTargets */ };

static void FindTargets(const BYTE* data, int width, int height,
 int aimbotHalf, ModeCandidate& aimbot,
 int silentHalf, ModeCandidate& silent,
 int flickerHalf, ModeCandidate& flicker);

static bool RefineHeadAnchor(const BYTE* data, int bufW, int bufH, int stride,
 const ModeCandidate& cand, int modeHalfFov,
 int& outDx, int& outDy);

static int ComputeMaxFov(); // returns max-of-active-modes' FOVs
```

### ScreenCapture (capture/ScreenCapture.h)

```cpp
class UltraOptimizedDXGICapture {
 bool Initialize();
 bool CaptureRegionAdaptive(int x, int y, int w, int h,
 BYTE** outData, UINT timeout = 0);
 bool CaptureRegionGPU(int x, int y, int w, int h,
 int& outDx, int& outDy, bool& outFound,
 UINT timeout = 0);
 void UploadLUT(const std::array<bool, 256*256*256>& lut);
 void Cleanup();
 int GetScreenWidth() const;
 int GetScreenHeight() const;
 bool IsGPUComputeReady() const;
};

bool InitializeOptimizedCapture(); // Singleton initializer
extern std::unique_ptr<UltraOptimizedDXGICapture> g_optimizedCapture;
```

### ColorDetector (capture/ColorDetector.h)

```cpp
class FastColorDetector {
 static void EnsureTable(); // Build LUT if needed
 static bool IsTargetColor(BYTE r, BYTE g, BYTE b); // O(1) lookup
 static void Invalidate(); // Force LUT rebuild
 static const std::array<bool, 256*256*256>& GetLookupTable();
};

class MonitorInfo {
 static int GetRefreshRate(); // EnumDisplaySettings + cache
};
```

### MouseMove (input/MouseMove.h)

```cpp
void apply_delta(int deltaX, int deltaY, double smooth); // Aimbot
void Magnet(int deltaX, int deltaY, double smooth); // Assist
void SnapShoot_P(int deltaX, int deltaY); // Silent aim
void SnapShoot_F(int deltaX, int deltaY); // Flicker
```

> **None of these modify `currentFOV`.** Capture-thread owns it
> exclusively (recomputed each iter via `ComputeMaxFov()`).

### UDPClient (network/UDPClient.h)

```cpp
bool ensureWinsock(); // Init WSA (idempotent)
bool initializeUDPSocket(const char* ip, int port);
void cleanupSocket();
void sendCommand(int xx, int yy, char prefix); // Send "<prefix><x>,<y>\r"
void sendClick(); // Send "L\r"
void sendArduinoConfig(char cmd, int value); // Send "<cmd><value>\r" (K)
bool reconnectSocket(); // Close + reinit
```

### ConfigManager (core/ConfigManager.h)

```cpp
class ConfigManager {
 static bool loadIPAndPort(std::string& ip, int& port);
 static bool loadConfig();
 static void saveConfig();
 static void saveConfig(const std::string& ip, int port);
};
```

### LicenseManager (security/LicenseManager.h)

```cpp
class LicenseManager {
 static bool checkLicense(); // Validates HWID against config file hash
};
```

### WebServer (network/WebServer.h)

```cpp
void initializeNetworking(int port); // Start web server + anti-debug + firewall
void closeApplication(); // Clean shutdown
```

### Globals (core/Globals.h)

```cpp
extern std::mutex fovMutex;
extern int currentFOV; // MAX-of-active-modes; written ONLY by capture thread

extern int Width, Height; // Screen dimensions (set at startup)
extern int oX, oY; // Aimbot's absolute screen-coord target (debug)

// Per-mode FILTERED coords - written by capture thread each frame.
// Each mode reads ONLY its own pair.
extern int apply_delta_x, apply_delta_y; // Aimbot (filtered by apply_delta_fov/2)
extern int mode_a_x, mode_a_y; // Silent (filtered by mode_a_fov/2)
extern int nonmode_a_x, nonmode_a_y; // Flicker (filtered by nonmode_a_fov/2)

extern std::atomic<uint64_t> capture_seq; // Bumps each successful frame
extern std::atomic<int> capture_fov_used; // = currentFOV under MAX-FOV arch
```

---

## Virtual Key Code Reference

Common VK codes used in configuration:

| Code | Constant | Key |
|------|----------|-----|
| 1 | VK_LBUTTON | Left mouse |
| 2 | VK_RBUTTON | Right mouse |
| 4 | VK_MBUTTON | Middle mouse |
| 5 | VK_XBUTTON1 | Mouse side button 1 (back) |
| 6 | VK_XBUTTON2 | Mouse side button 2 (forward) |
| 16 | VK_SHIFT | Shift |
| 17 | VK_CONTROL | Ctrl |
| 18 | VK_MENU | Alt |
| 20 | VK_CAPITAL | Caps Lock |
