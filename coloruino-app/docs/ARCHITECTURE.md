# Coloruino Architecture

> **Status**: This document reflects the **MAX-FOV + per-mode filter**
> architecture (post-2026-05-31 refactor). For the historical model see
> the CHANGELOG.

## System Diagram

```
+---------------------------------------------------------------------+
| Coloruino PC Application |
| |
| +--------------+ +--------------+ +----------------------+ |
| | main.cpp | | WebServer | | AntiDebug | |
| | | | (port 13548) | | (background) | |
| | Startup | | | | | |
| | License | | 51 routes | | PEB, DebugPort, | |
| | Thread mgmt | | Mobile HTML | | DebugObject, RDTSC, | |
| | | | Search/Tabs | | HW breakpoints, | |
| | | | Config save | | Heap flags | |
| | | | Auth check | | | |
| +------+-------+ +--------------+ +----------------------+ |
| | |
| +----v--------------------------------------------------------+ |
| | Capture Thread | |
| | | |
| | +-----------------+ +--------------+ +--------------+ | |
| | | ComputeMaxFov() |-->| DXGI Capture |-->| FindTargets | | |
| | | each iter | | | | per-mode | | |
| | | = max(active | | AcquireNext | | | | |
| | | modes' FOVs) | | Frame(1ms) | | 3× candidate | | |
| | +-----------------+ +--------------+ | tracking + | | |
| | | FOV mask | | |
| | +------+-------+ | |
| | | | |
| | +-----------v-----+ | |
| | | Per-mode | | |
| | | Post-Process | | |
| | | | | |
| | | CountNeighbours | | |
| | | (per mode) | | |
| | | RefineHead | | |
| | | Anchor | | |
| | | Dead-Body Filter| | |
| | +---------+-------+ | |
| | | | |
| | +--------------v------+ | |
| | | Per-mode globals | | |
| | | apply_delta_x/y | | |
| | | mode_a_x/y | | |
| | | nonmode_a_x/y | | |
| | | capture_seq++ | | |
| | +----------+----------+ | |
| | | | |
| | +----------v----------+ | |
| | | Mouse Output Calls | | |
| | | apply_delta() | | |
| | | Magnet() | | |
| | | Otrigger_action() | | |
| | | (polygon check | | |
| | | or legacy) | | |
| | +----------+----------+ | |
| +---------------------------------------------+---------------+ |
| | |
| +------------------+ +------------------+ | |
| | mode_a Thread | | nonmode_a Thread | | |
| | | | | | |
| | Reads: | | Reads: | | |
| | mode_a_x/y | | nonmode_a_x/y | | |
| | capture_seq | | capture_seq | | |
| | | | | | |
| | Edge detect | | Edge detect | | |
| | 20ms debounce | | 20ms debounce | | |
| | | | | | |
| | Fast path: fire | | Fast path: fire | | |
| | Slow path: | | Slow path: | | |
| | WaitForFresh | | WaitForFresh | | |
| | Capture (2 frm) | | Capture (2 frm) | | |
| | | | | | |
| | SnapShoot_P() | | SnapShoot_F() | | |
| +--------+---------+ +--------+---------+ | |
| | | | |
| +-----------+-----------+ | |
| | | |
| +----v------------------------v----------+ |
| | UDP Client | |
| | sendCommand(x, y, prefix) | |
| | Format: "<prefix><x>,<y>\r" | |
| +--------------------+-------------------+ |
| | |
+-----------------------------------------+---------------------------+
 | UDP
 v
 +--------------+
 | Arduino |
 | HID Mouse |
 +--------------+
```

---

## Thread Model

| Thread | Priority | Pacing | Shared State |
|--------|----------|--------|--------------|
| Capture | High (`set_thread_high_priority`) | DXGI `AcquireNextFrame(1ms)` kernel-blocks until vblank | **Writes**: `currentFOV`, `apply_delta_x/y`, `mode_a_x/y`, `nonmode_a_x/y`, `oX/oY`, `capture_seq`. Reads cfg::* live each iter. |
| mode_a | `THREAD_PRIORITY_HIGHEST` | `sleep_for(1ms)` polling | Reads: `mode_a_x/y`, `capture_seq`. |
| nonmode_a | `THREAD_PRIORITY_HIGHEST` | `sleep_for(1ms)` polling | Reads: `nonmode_a_x/y`, `capture_seq`. |
| WebServer | Normal | `accept()` blocks | Reads/writes `cfg::*` values. Saves config. |
| AntiDebug | Normal | Continuous loop | Reads system state. Calls `KillSelf()` on detection. |
| Main | Normal | `sleep_for(1hr)` keepalive | Setup only |

### `currentFOV` Ownership (post-refactor)

**Old (pre-2026-05-31)**: Multiple threads wrote `currentFOV` competitively
(aimbot, assist, silent, flicker). Caused FOV race -> silent aim choke.

**New**: Capture thread is the **sole writer**. Computed each iter via
`ComputeMaxFov()`:

```cpp
int ComputeMaxFov() {
 int m = 0;
 if (apply_delta_ativo && apply_delta_fov > m) m = apply_delta_fov;
 if (apply_deltaassist_ativo && apply_deltaassist_fov > m) m = apply_deltaassist_fov;
 if (mode_a_ativo && mode_a_fov > m) m = mode_a_fov;
 if (nonmode_a_ativo && nonmode_a_fov > m) m = nonmode_a_fov;
 if (m <= 0) m = 200; // safety
 return m;
}
```

Web-UI toggles are picked up on the **next capture iteration** - no
signalling required.

`fovMutex` remains for the capture-thread store (`currentFOV = w`) - kept
for legacy compatibility with any read-side code path that might still
take it.

---

## Capture Loop (FindTargets per-mode)

Single linear scan over the MAX-FOV buffer, masking each candidate by
its mode's own FOV box:

```
for each row y in [0, h):
 skip row if |dy| > max(aimbotHalf, silentHalf, flickerHalf)

 for each col x in [0, w):
 if !IsTargetColor(px): continue

 compute dx, dy, dist²

 if active(aimbot) && |dx| ≤ aimbotHalf && |dy| ≤ aimbotHalf:
 update aimbot's closest + topmost
 if active(silent) && |dx| ≤ silentHalf && |dy| ≤ silentHalf:
 update silent's closest + topmost
 if active(flicker) && |dx| ≤ flickerHalf && |dy| ≤ flickerHalf:
 update flicker's closest + topmost
```

Cost: O(W·H) where W=H=MAX FOV. At a typical 100×100 with sparse purple
matches, ~10k LUT lookups + 3 cheap branches per hit. Sub-millisecond.

Per-mode `ModeCandidate` struct:

```cpp
struct ModeCandidate {
 int bestDist2 = INT_MAX;
 int bestCDx, bestCDy; // closest-to-centre delta
 int bestTopY = INT_MAX;
 int bestTDx, bestTDy; // topmost-Y delta
 bool found = false;
};
```

---

## Post-Processing per Mode

After `FindTargets`, `OptimizedProcessImage` runs three independent
processing chains:

### Aimbot
```
if cluster check passes:
 apply_delta_x = aimbot.bestTDx + target_offset_x
 apply_delta_y = aimbot.bestTDy + target_offset_y
```

### Silent
```
if cluster check passes:
 if mode_a_head_targeting:
 (aim_dx, aim_dy) = RefineHeadAnchor(silent)
 else:
 (aim_dx, aim_dy) = (silent.bestCDx, silent.bestCDy)

 if dead_body_filter && |aim_dy - prev_dy| > threshold:
 suppress this frame
 else:
 mode_a_x = aim_dx + mode_a_target_offset_x
 mode_a_y = aim_dy + mode_a_target_offset_y
```

### Flicker
```
if cluster check passes:
 if mode_a_head_targeting:
 (aim_dx, aim_dy) = RefineHeadAnchor(flicker)
 else:
 (aim_dx, aim_dy) = (flicker.bestCDx, flicker.bestCDy)
 nonmode_a_x = aim_dx + mode_a_target_offset_x
 nonmode_a_y = aim_dy + mode_a_target_offset_y
```

### Per-Mode Cluster Validation

`CountNeighbours` is called **per mode**: a mode whose closest pixel
has < `cfg::min_cluster_size` purple neighbours is dropped without
affecting the other modes. Costs 8 LUT lookups × 3 modes = 24 - trivial.

---

## RefineHeadAnchor (Tier 1 port from tfirm)

Three steps on the buffer for any mode that found a target:

### Step 1: Walk-Down (height estimation)

```
start_y = cand.bestTopY
start_x = cand.bestTDx + halfW (delta -> buffer coord)

gap = 0
bot_y = start_y
for y in [start_y+1, bufH):
 if IsTargetColor(pixel at (start_x, y)):
 bot_y = y; gap = 0
 else:
 if ++gap > head_anchor_gap_tolerance: break

height = bot_y - start_y + 1
```

Walks down the topmost pixel's column, tolerating up to N consecutive
non-purple rows (handles broken outline / hair gaps).

### Step 2: Shoulder-Band X Averaging

```
band_rows = (head_anchor_band_rows > 0)
 ? head_anchor_band_rows
 : clamp(height/4, 2, 6)

sum_x = 0; n = 0
for y in [start_y, min(bufH, start_y + band_rows)):
 for x in [halfW - modeHalfFov, halfW + modeHalfFov]:
 if IsTargetColor(pixel at (x, y)):
 sum_x += x; n++

anchor_x = (n > 0) ? sum_x / n : start_x
```

Averages X across the top N rows of purple pixels inside the mode's FOV.
At range, the band catches both shoulder blobs -> midpoint = body centre
directly under the head.

### Step 3: Proportional Y Offset

```
head_off = 0
if height ≥ head_anchor_close_min_h:
 head_off = max(3, height * head_anchor_close_pct / 100)
elif height ≥ head_anchor_mid_min_h:
 head_off = max(1, height * head_anchor_mid_pct / 100)
// else tiny target - leave head_off = 0 (aim crown)

aim_dx = anchor_x - halfW (back to delta)
aim_dy = start_y - halfH + head_off
```

Adapts to enemy distance. Tall close enemies get a bigger offset toward
the forehead; tiny far enemies get 0 so we don't over-shoot above the
head.

---

## Silent Aim Fast/Slow Path

`mode_a()` and `nonmode_a()` use the same pattern:

```
On key edge (after 20ms debounce):
 seqBefore = capture_seq.load(acquire)
 (mx, my) = read per-mode coord global (mode_a_x/y or nonmode_a_x/y)

 if (mx == 0 && my == 0): // slow path
 WaitForFreshCapture(seqBefore, FRESH_TIMEOUT_MS,
 coord_ref, mx_out, my_out)

 if (mx != 0 || my != 0):
 SnapShoot_P(mx, my) / SnapShoot_F(mx, my)
```

### WaitForFreshCapture

```cpp
static bool WaitForFreshCapture(uint64_t seqBefore, int timeoutMs,
 const int& coordsX, const int& coordsY,
 int& outX, int& outY);
```

- Loops across multiple seq advances, not just one.
- Exits when coords become non-zero AND seq > seqBefore (snapshots
 values atomically with the acquire-load).
- Returns false (with `outX = outY = 0`) on timeout.
- Timeout = `ComputeFreshTimeoutMs()` = `(2 × 1000 / refresh) + 2 ms`
 floored at 6 ms.

### Why 2 Frames

The first capture iter after the key press may have been **in-flight**
when the key landed - its `AcquireNextFrame` already grabbed a desktop
frame rendered before the target became visible. The second iter is the
first one fully started after the press. 2 frames catches both cases
while staying below human perception.

| Refresh | Timeout |
|---|---|
| 60 Hz | 35 ms |
| 144 Hz | 16 ms |
| 200 Hz | 12 ms |
| 240 Hz | 10 ms |
| 360 Hz | 7 ms |
| 500 Hz | 6 ms (floored) |

---

## GPU vs CPU Path

### CPU Path (default)

```
ComputeMaxFov() -> w
AcquireNextFrame(1ms)
 -> CopySubresourceRegion to regionStagingTexture
 -> Map to CPU memory
 -> FindTargets (per-mode candidates)
 -> OptimizedProcessImage (cluster + RefineHeadAnchor + dead body)
 -> capture_fov_used.store(w, relaxed)
 -> capture_seq.fetch_add(1, release) // publishes everything above
 -> apply_delta() + Magnet() + Otrigger_action()
```

### GPU Path (`cfg::use_gpu_processing = true`)

```
ComputeMaxFov() -> w (must be ≤ 255 due to packing)
AcquireNextFrame(1ms)
 -> CopySubresourceRegion to gpuCaptureTex (SRV-bindable)
 -> Dispatch compute shader (cs_5_0)
 -> Readback 4-byte result buffer
 -> Decode: (dist² << 16) | (y << 8) | x
 -> ProcessGPUResult - filters single result per-mode by each mode's FOV
 -> publish seq + run downstream
 -> On GPU failure (FOV > 255): fall back to CPU path
```

**GPU limitations**:
- 255×255 max FOV (8-bit packing).
- Single closest-pixel result - no per-mode candidates, no topmost-Y
 tracking.
- Multi-target scenes where the overall-closest pixel falls outside
 aimbot's FOV will leave aimbot with 0,0 even if a different target
 IS inside it.
- Default OFF.

### Compute Shader (HLSL CS 5.0)

Per-thread:
1. Sample capture texture SRV at `threadId.xy`.
2. Look up `(R, G, B)` in 256³ 3D LUT (R8_UNORM).
3. If match: `dist² = dx² + dy²`; `InterlockedMin(result, packed)`.

---

## Color Detection Pipeline

```
Pixel RGB
 |
 +--> RGB Range Check: menorRGB[i] ≤ channel ≤ maiorRGB[i]
 |
 +--> HSV Range Check (if useIstrigFilter):
 |
 +-- Integer HSV conversion (no float):
 | max = max(R,G,B), min = min(R,G,B)
 | S = (max-min) * 100 / max
 | V = max * 100 / 255
 | H = 60 * sector + offset (degrees 0-360)
 |
 +-- Range: menorHSV[i] ≤ channel ≤ maiorHSV[i]
 Special case: Red hue wraps (h ≤ 30 || h ≥ 330)

Both must pass -> LUT[R*65536 + G*256 + B] = true
```

LUT rebuilt whenever color mode or filter toggle changes. 16 MB built in
~50 ms.

---

## Triggerbot (Otrigger_action)

Lives at the end of the capture loop. Uses the same captured buffer.

### Two Modes

#### Polygon (default, `cfg::trigger_polygon_check = true`)

```
For r in [1, maxRadius]:
 test pixel at center + (+r, 0) -> hitPlusX
 test pixel at center + (-r, 0) -> hitMinusX
 test pixel at center + (0, +r) -> hitPlusY
 test pixel at center + (0, -r) -> hitMinusY
 short-circuit if all 4 hit

if all 4 rays hit purple -> fire 'L'
```

4-ray crossing test. Rejects:
- Single-sided UI purples (HP bars, ability icons).
- Crosshair just outside enemy outline (away-side rays escape).

#### Legacy (`cfg::trigger_polygon_check = false`)

Spiral-first-hit: fires on any purple pixel within trigger FOV.

### Color Match

Both modes use the same channel-wise tolerance check against the active
color-mode's reference color:

```cpp
abs(px[2] - targetR) < pixel_sens &&
abs(px[1] - targetG) < pixel_sens &&
abs(px[0] - targetB) < pixel_sens
```

`pixel_sens = 90` (hardcoded). Reference color comes from
`cfg::color_mode` switch (e.g. mode 0 = `RGB(235, 105, 254)`).

### Guards

- `cfg::trigger_action_ativo` enabled.
- `cfg::trigger_action_key` held.
- `has_shot` flag (one fire per key-hold).
- `VK_LBUTTON` NOT held (prevents conflict with manual click).

---

## Smoothing System

### apply_delta (Aimbot)

Per-frame with overflow accumulation + optional distance scaling:

```cpp
// Distance-aware multiplier
dist_factor = 1.0;
if (apply_delta_dist_smoothing) {
 dist² = deltaX² + deltaY²
 if dist² < near_dist² -> dist_factor = near_mult (default 0.4)
 elif dist² < mid_dist² -> dist_factor = mid_mult (default 0.7)
}

// Overflow accumulation
exact_x = (deltaX / smooth) * speed * dist_factor + overflow_x
exact_y = (deltaY / smooth) * speed * dist_factor + overflow_y

moveX = int(exact_x)
moveY = int(exact_y)

overflow_x = exact_x - moveX // carry fractional part
overflow_y = exact_y - moveY
```

Overflow resets to 0 when:
- Feature disabled
- Activation key released
- Delta is (0, 0)

### Magnet (Assist)

Identical structure but uses `apply_deltaassist_smooth` and
`assist_speed`. NO distance-aware scaling. Reads `apply_delta_x/y`
(tracks aimbot's target).

### SnapShoot_P (Silent)

```cpp
moveX = deltaX * cfg::distance
moveY = deltaY * cfg::distance
sendCommand(moveX, moveY, 'P')
```

Linear gain. No smoothing, no overflow.

### SnapShoot_F (Flicker)

Identical formula with `cfg::nonmode_a_distance`, prefix `'F'`.

---

## Security Layers

### Compile-Time
- `xorstr_()` - all user-visible strings encrypted at compile time,
 decrypted at runtime on stack.
- VMProtect SDK markers: `VMProtectBeginUltra`, `VMProtectBeginMutation`,
 `VMProtectEnd`.
- FNV-1a compile-time hash for license key comparison (no plaintext key
 in binary).

### Runtime
- HWID binding - config file locked to specific hardware.
- Anti-debug thread - continuous monitoring (PEB, DebugPort,
 DebugObject, RemoteDebugger, HW breakpoints, heap flags, RDTSC).
- HTTP auth - web config panel requires Basic auth.
- Config encryption - XOR cipher on `data` file.

### Network
- COM firewall rule - created silently via `INetFwPolicy2`, no
 `netsh.exe` child process visible in Process Monitor.
- Firewall rule named "AMD Radeon Software Helper" (matches binary metadata).
- WebUI port: 13548 (above well-known service map, low collision risk).
- Outbound UDP to Arduino: DNS-shape on port 5353 (mDNS port).
- Non-blocking UDP - no connection state to detect.

### Disguise
- Binary metadata (FileDescription, ProductName, CompanyName): AMD Radeon Software.
- Firewall rule display name: AMD Radeon Software Helper.
- Web UI title and brand label: "Spotify Web Player" - intentional
 split-identity (visual cover for over-shoulder glance while keeping
 the underlying binary AMD-branded for forensic-tool consistency).
- Favicon: green Spotify SVG (inline base64).
- Signed: Authenticode self-signed cert with subject `CN=AMD Radeon Software`.
