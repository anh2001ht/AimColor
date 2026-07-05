# Coloruino Technical Tuning Reference

Deep-dive into every configuration variable - the math, the interactions, the edge cases, and the exact code paths.

> **Architecture note (post-2026-05-31)**: The capture region size is now
> **MAX of all active modes' FOVs** (computed each iter by
> `ComputeMaxFov()`). Each mode's coords are filtered by its OWN
> half-FOV inside `FindTargets`. The legacy "mode writes currentFOV"
> race no longer exists.

---

## Variable Index

| Variable | Type | Range | Default | Feature |
|----------|------|-------|---------|---------|
| `apply_delta_ativo` | bool | 0/1 | 1 | Aimbot |
| `apply_deltakey1` | int | 0-255 | 5 (XButton1) | Aimbot |
| `apply_deltakey2` | int | 0-255 | 16 (Shift) | Aimbot |
| `target_offset_x` | int | -50-50 | 1 | Aimbot |
| `target_offset_y` | int | -20-100 | 5 | Aimbot |
| `apply_delta_fov` | int | 1-200 | 82 | Aimbot |
| `apply_delta_smooth` | float | 1.0-4.0 | 1.4 | Aimbot |
| `speed` | float | 0.1-1.5 | 0.4 | Aimbot |
| `sleep` | int | 0-100 | 0 | Aimbot |
| `apply_delta_dist_smoothing` | bool | 0/1 | 1 | Aimbot (**NEW 2026-05-31**) |
| `apply_delta_near_dist` | int | 1-80 | 10 | Aimbot (**NEW**) |
| `apply_delta_mid_dist` | int | 5-200 | 30 | Aimbot (**NEW**) |
| `apply_delta_near_mult` | float | 0.05-2.0 | 0.4 | Aimbot (**NEW**) |
| `apply_delta_mid_mult` | float | 0.05-2.0 | 0.7 | Aimbot (**NEW**) |
| `apply_deltaassist_ativo` | bool | 0/1 | 0 | Assist |
| `assist_apply_deltakey` | int | 0-255 | 18 (Alt) | Assist |
| `assist_target_offset_x` | int | - | 2 | Assist (unused - reads aimbot's) |
| `assist_target_offset_y` | int | - | 3 | Assist (unused - reads aimbot's) |
| `apply_deltaassist_fov` | int | 1-200 | 1 | Assist (contributes to MAX-FOV) |
| `apply_deltaassist_smooth` | float | 1.0-4.0 | 1.5 | Assist |
| `assist_speed` | float | - | 1.0 | Assist |
| `mode_a_ativo` | bool | 0/1 | 1 | Silent Aim |
| `mode_a_key` | int | 0-255 | 6 (XButton2) | Silent Aim |
| `mode_a_target_offset_x` | int | -100-100 | 0 | Silent Aim |
| `mode_a_target_offset_y` | int | -100-100 | 3 | Silent Aim |
| `mode_a_fov` | int | 1-200 | 100 | Silent Aim |
| `mode_a_delay_between_shots` | int | 0-10 | 1 | Silent Aim (unused - cooldown on FW) |
| `distance` | float | 0.001-10.0 | 2.62 | Silent Aim |
| `nonmode_a_ativo` | bool | 0/1 | 0 | Flicker |
| `nonmode_a_key` | int | 0-255 | 6 (XButton2) | Flicker |
| `nonmode_a_fov` | int | 1-200 | 100 | Flicker |
| `nonmode_a_delay_between_shots` | int | 1-50 | 1 | Flicker |
| `nonmode_a_distance` | float | 0.1-10.0 | 2.5 | Flicker |
| `trigger_action_ativo` | bool | 0/1 | 1 | Triggerbot |
| `trigger_action_key` | int | 0-255 | 18 (Alt) | Triggerbot |
| `trigger_action_delay` | int | - | 1 | Triggerbot (unused) |
| `trigger_action_fovX` | int | 1-20 | 3 | Triggerbot |
| `trigger_action_fovY` | int | 1-20 | 3 | Triggerbot |
| `trigger_polygon_check` | bool | 0/1 | 1 | Triggerbot (**NEW 2026-05-31**) |
| `mode_a_head_targeting` | bool | 0/1 | 1 | Silent Aim |
| `mode_a_cooldown_ms` | int | 50-500 | 50 | Silent Aim |
| `head_anchor_proportional` | bool | 0/1 | 1 | Silent + Flicker (**NEW 2026-05-31**) |
| `head_anchor_band_rows` | int | 0-20 | 0 (auto) | Silent + Flicker (**NEW**) |
| `head_anchor_gap_tolerance` | int | 0-10 | 2 | Silent + Flicker (**NEW**) |
| `head_anchor_close_pct` | int | 0-50 | 18 | Silent + Flicker (**NEW**) |
| `head_anchor_mid_pct` | int | 0-50 | 10 | Silent + Flicker (**NEW**) |
| `head_anchor_close_min_h` | int | 5-200 | 30 | Silent + Flicker (**NEW**) |
| `head_anchor_mid_min_h` | int | 1-100 | 10 | Silent + Flicker (**NEW**) |
| `color_mode` | int | 0-3 | 0 | Color |
| `useIstrigFilter` | bool | 0/1 | 1 | Color |
| `use_gpu_processing` | bool | 0/1 | 0 | Performance |
| `dead_body_filter` | bool | 0/1 | 0 | Filtering |
| `dead_body_threshold` | int | 3-60 | 15 | Filtering |
| `min_cluster_size` | int | 0-8 | 2 | Filtering |

> **Removed in firmware v2** (no longer in this list): `mode_a_split_delay_us`.
> The P command uses a deterministic 4-report sequence with no
> artificial delay between reports.

---

## 1. Aimbot (apply_delta) - Detailed

### Execution Path

```
CaptureScreen() [capture thread, DXGI-paced]
 -> ComputeMaxFov() [picks max of active modes' FOVs]
 -> AcquireNextFrame(1ms) [blocks until frame ready, can return TIMEOUT]
 -> FindTargets(data, w, h, aimbotHalf, &aimbot, silentHalf, &silent,
 flickerHalf, &flicker)
 -> CountNeighbours per mode [cluster validation, mode-independent]
 -> For aimbot output (only if aimbot.found):
 apply_delta_x = aimbot.bestTDx + cfg::target_offset_x
 apply_delta_y = aimbot.bestTDy + cfg::target_offset_y
 -> capture_seq.fetch_add(1, release) [publishes globals to threads]
 -> apply_delta(apply_delta_x, apply_delta_y, cfg::apply_delta_smooth)
```

### apply_delta() Smoothing Formula

```cpp
// Distance-aware multiplier (NEW 2026-05-31)
dist_factor = 1.0;
if (cfg::apply_delta_dist_smoothing) {
 dist² = deltaX² + deltaY²;
 if (dist² < near_dist²) dist_factor = near_mult; // default 0.4
 else if (dist² < mid_dist²) dist_factor = mid_mult; // default 0.7
}

// Per-frame with overflow accumulation
exact_x = (deltaX / smooth) * speed * dist_factor + overflow_x
exact_y = (deltaY / smooth) * speed * dist_factor + overflow_y

moveX = int(exact_x) // truncate toward zero
moveY = int(exact_y)

overflow_x = exact_x - moveX // fractional remainder carried forward
overflow_y = exact_y - moveY

sendCommand(moveX, moveY, 'M')
```

**Key properties:**
- Overflow accumulation ensures sub-pixel movements are never lost. A 0.3px/frame movement accumulates to 1px every ~3 frames.
- `int()` truncates toward zero, not rounds. This means movements slightly favor undershooting.
- Overflow resets to 0 when: feature disabled, key released, or delta is (0,0).

### Effective Movement Per Frame

Given target at pixel offset `(dx, dy)`:

```
movement_per_frame = pixel_offset × (speed / smooth)
frames_to_reach = 1 / (speed / smooth) = smooth / speed
```

| Smooth | Speed | Effective Rate | Frames to Reach |
|--------|-------|---------------|-----------------|
| 1.0 | 1.0 | 100% per frame | 1 |
| 1.4 | 0.4 | 28.6% per frame | ~3.5 |
| 2.0 | 0.8 | 40% per frame | ~2.5 |
| 3.0 | 0.3 | 10% per frame | ~10 |
| 1.0 | 0.1 | 10% per frame | ~10 |

Note: "frames to reach" is approximate - the delta shrinks each frame as you get closer, so it's exponential decay, not linear. The target is never reached in exactly N frames; it asymptotically approaches.

### FOV Interaction with Other Features (post-2026-05-31)

**Old model (deprecated)**: Each mode wrote `currentFOV` competitively ->
silent aim FOV race bug -> "choke on first fire."

**New model**: Capture thread is the **sole writer**. Each iter:

```
w = ComputeMaxFov(); // max of active modes' FOVs
capture region = w × w
FindTargets(..., aimbotHalf=apply_delta_fov/2 if active else 0,
 silentHalf=mode_a_fov/2 if active else 0,
 flickerHalf=nonmode_a_fov/2 if active else 0)
```

Each mode's candidate is FILTERED to its own FOV box during the scan.
Per-mode coords (`apply_delta_x/y`, `mode_a_x/y`, `nonmode_a_x/y`) are
the result of that filtering. No mode writes `currentFOV` ever.

**Web-UI changes** to FOV sliders or activation toggles take effect on
the very next capture iter (no signalling needed). Live response.

**Practical effect**: If aimbot FOV is 62 and silent FOV is 80, capture
is 80×80. Aimbot only "sees" pixels within ±31 of centre (its own FOV
mask). Silent sees full 80 region. Both work simultaneously without
interference.

### Activation Keys - OR Logic

```cpp
if (!GetAsyncKeyState(cfg::apply_deltakey1) && !GetAsyncKeyState(cfg::apply_deltakey2)) {
 overflow_x = overflow_y = 0;
 return;
}
```

Either key activates. When neither is pressed, overflow is flushed (no residual drift).

### target_offset_x / target_offset_y

Applied BEFORE the delta enters `apply_delta()`:

```cpp
apply_delta_x = raw_dx + cfg::target_offset_x;
apply_delta_y = raw_dy + cfg::target_offset_y;
```

`raw_dx` is the pixel offset from center of the capture region to the closest matching pixel. `target_offset` shifts the aim point.

**Coordinate system:**
- Positive X = right
- Positive Y = down
- raw_dx negative = target is left of center
- raw_dy negative = target is above center

**Example:** Target outline pixel at (-3, -8) relative to center. With offset (1, 5):
- apply_delta_x = -3 + 1 = -2 (aim 2px left of center)
- apply_delta_y = -8 + 5 = -3 (aim 3px above center)

The offset moves the aim point from the outline edge toward the body center.

---

## 2. Silent Aim (mode_a) - Detailed

### Execution Path (post-2026-05-31)

```
mode_a() [dedicated thread, THREAD_PRIORITY_HIGHEST, 1ms polling]
 -> GetAsyncKeyState(cfg::mode_a_key) [edge detection]
 -> 20ms debounce check
 -> seqBefore = capture_seq.load(acquire)
 -> mx = mode_a_x; my = mode_a_y // snapshot per-mode coords
 -> if (mx == 0 && my == 0): // slow path
 WaitForFreshCapture(seqBefore, FRESH_TIMEOUT_MS,
 mode_a_x, mode_a_y, mx, my)
 -> if (mx != 0 || my != 0):
 SnapShoot_P(mx, my)
```

`FRESH_TIMEOUT_MS = (2 × 1000 / refresh) + 2` floored at 6 ms. See
ARCHITECTURE.md for the rationale on 2-frame timeout.

`WaitForFreshCapture` loops across multiple seq advances (not just one)
 - catches "target appeared between iters" cases. Snapshots coords
atomically with the seq acquire-load (no torn reads).

### SnapShoot_P Formula

```cpp
float mult = cfg::distance > 0.0f ? cfg::distance : 1.0f;
int moveX = static_cast<int>(deltaX * mult);
int moveY = static_cast<int>(deltaY * mult);
sendCommand(moveX, moveY, 'P');
```

The old code used normalize -> clamp(10) -> multiply, which was algebraically equivalent to `deltaX * distance` in all cases (the clamp and normalize cancelled). Now simplified to the direct multiplication.

**Properties:**
1. `distance` is a pure linear multiplier: `HID_units = pixels × distance`
2. Movement is proportional to pixel offset at all ranges
3. No intermediate float operations (sqrt/normalize removed)

### What `distance` Must Equal

For a perfect hit, the HID units sent must equal the cursor movement needed in-game to cover the pixel offset.

```
distance = HID_counts_per_screen_pixel
```

This depends on:

| Factor | Effect |
|--------|--------|
| In-game sensitivity | Lower sens -> more counts per pixel -> higher distance |
| Game's internal sensitivity formula | Each game converts counts differently |
| Resolution | Higher res -> more pixels per degree -> different ratio |
| Game FOV | Wider FOV -> more pixels per degree at screen center |
| Windows pointer speed | Should be 6/11 (1:1). Other values scale linearly |

**Estimation for Valorant (103° HFOV, 1920×1080):**

```
degrees_per_HID_count = sensitivity × 0.07
 = 0.34 × 0.07 = 0.0238°

degrees_per_pixel = horizontal_FOV / horizontal_resolution
 = 103 / 1920 = 0.05365°

distance = degrees_per_pixel / degrees_per_HID_count
 = 0.05365 / 0.0238 ≈ 2.25
```

**Note:** This is theoretical. In practice, games may apply additional scaling, rounding, or nonlinear transforms. Empirical calibration is required.

### Why the Clamp Exists (Historical)

The original UCAimColor source capped `dist` at 10.0 to limit maximum movement magnitude. But because it divides by `dist` and multiplies by `dist × multiplier`, the clamp is algebraically inert. It was likely intended to limit the movement range but the formula structure defeats its purpose.

If you wanted an actual distance limit, you'd need:

```cpp
// To actually limit max movement:
if (dist > MAX_RANGE) return; // skip shot entirely if too far
```

### Head Targeting (mode_a_head_targeting)

When enabled, silent and flicker use the **topmost-Y** purple pixel as
the aim anchor instead of the closest-to-centre pixel.

```cpp
// In OptimizedProcessImage:
if (silent.found) {
 int aim_dx, aim_dy;
 if (cfg::mode_a_head_targeting) {
 // Topmost-Y, optionally refined by RefineHeadAnchor (see §6 below).
 RefineHeadAnchor(screenData, w, h, stride, silent, silentHalf,
 aim_dx, aim_dy);
 } else {
 aim_dx = silent.bestCDx; // closest-to-centre
 aim_dy = silent.bestCDy;
 }
 // ... apply offset, dead-body filter, write mode_a_x/y
}
```

When `cfg::head_anchor_proportional` is ALSO on (default), `RefineHeadAnchor`
adds shoulder-band X averaging + proportional Y offset on top of the
bare topmost-Y pixel - see §6 below.

**Impact on offsets:** With head targeting ON + proportional refinement
ON, `aim_dy` already lands at forehead/eyes (scaled by enemy height).
`mode_a_target_offset_y` should be small (0-3) since the starting point
is already on the head.

### Dead Body Filter

```cpp
static int prev_aim_dy = 0;
static bool prev_valid = false;
if (cfg::dead_body_filter && prev_valid) {
 if (FastAbs(aim_dy - prev_aim_dy) > cfg::dead_body_threshold) {
 mode_a_x = 0; mode_a_y = 0; // suppress this frame
 return;
 }
}
prev_aim_dy = aim_dy;
prev_valid = true;
```

Ragdolled corpses cause large Y jumps between frames. The filter suppresses silent aim for that frame, preventing shots at dead bodies. Only affects `mode_a_x/y` - aimbot tracking continues normally.

### Cluster Validation (min_cluster_size)

```cpp
if (found && cfg::min_cluster_size > 0) {
 if (CountNeighbours(screenData, w, h, raw_dx, raw_dy) < cfg::min_cluster_size)
 found = false; // reject isolated pixel
}
```

Checks 8 neighbouring pixels around the spiral search hit. Rejects single-pixel noise from UI elements, particle effects, or DXGI artefacts. Cost: 8 LUT lookups (data in L1 cache).

### mode_a_target_offset_x / mode_a_target_offset_y

Applied in the capture thread, separate from aimbot offsets:

```cpp
mode_a_x = aim_dx + cfg::mode_a_target_offset_x;
mode_a_y = aim_dy + cfg::mode_a_target_offset_y;
```

**Range: -100 to 100** (wider than aimbot's 0-20 range).

These are independent from the aimbot's target_offset_x/y. Changing aimbot offsets does NOT affect silent aim, and vice versa.

### Edge Detection + Debounce

```cpp
if (key_is_down && !key_was_down && (now - lastFireTime) > DEBOUNCE) {
 // fire
 lastFireTime = now;
}
key_was_down = key_is_down;
```

**Why 100ms debounce:** The Arduino P command sends two HID reports: move+click, then snapback+release. The snapback momentarily releases all buttons. If the silent aim key is a mouse button (XButton2), the OS sees it released for one USB frame (~1ms), then re-pressed by the next real mouse report. Without debounce, the mode_a thread would see this as a new keypress edge and fire again.

**Arduino-side cooldown:** Additional 200ms P_COOLDOWN prevents rapid-fire even if the PC sends multiple P commands quickly.

### Accuracy Factors - Ranked by Impact

1. **`distance` calibration** - wrong value = consistent over/undershoot
2. **`mode_a_head_targeting`** - when ON, aims at head top instead of nearest body pixel
3. **`dead_body_filter`** - prevents wasted shots on ragdolled corpses
4. **`min_cluster_size`** - rejects single-pixel noise that causes random misfires
5. **`mode_a_target_offset_y`** - compensates for head targeting landing at outline top vs hitbox center
6. **`mode_a_fov`** - larger FOV = more chance of detecting body pixels at range
7. **Split delay** - separating move and click into distinct USB frames can improve hit registration
8. **Latency** - ~5-15ms from detection to HID report execution; moving targets shift
9. **Integer truncation** - `int()` loses up to 0.99 per axis; worst at small deltas

---

## 3. Triggerbot (trigger_action) - Detailed

### Execution Path

```
CaptureScreen() [after aimbot processing]
 -> Otrigger_action(screenData, capW, capH)
 -> Check: trigger_action_key held?
 -> Check: has_shot flag (one shot per hold)
 -> Check: VK_LBUTTON not held (prevent double-fire)
 -> Determine scan buffer:
 CPU path: reuse aimbot's capture buffer (zero-copy)
 GPU path: separate CaptureRegionAdaptive call
 -> Spiral scan within trigger FOV
 -> On match: sendCommand(0, 0, 'L')
```

### Color Matching (Triggerbot-Specific)

The triggerbot uses **fixed color values per mode**, not the LUT:

```cpp
switch (cfg::color_mode) {
 case 0: case 1: pixel_color = RGB(235, 105, 254); break; // purple
 case 2: pixel_color = RGB(255, 255, 85); break; // yellow
 case 3: pixel_color = RGB(254, 99, 106); break; // red
}
```

With a fixed tolerance of `pixel_sens = 90` per channel:

```cpp
abs(px.R - targetR) < 90 &&
abs(px.G - targetG) < 90 &&
abs(px.B - targetB) < 90
```

**This is different from the aimbot's LUT-based detection.** The triggerbot checks a single reference color ± 90 per channel, while the aimbot uses the full RGB range + optional HSV filter. This means the triggerbot may fire at slightly different pixels than the aimbot detects.

### trigger_action_fovX / trigger_action_fovY

The scan area is `(fovX * 2) × (fovY * 2)` pixels centered on screen:

```cpp
int trigW = cfg::trigger_action_fovX * 2;
int trigH = cfg::trigger_action_fovY * 2;
```

fovX = 3 -> 6 pixel wide scan area. This is intentionally tiny - the triggerbot should only fire when the target is nearly on crosshair.

### CPU vs GPU Buffer Reuse

```
if (aimData && aimW >= trigW && aimH >= trigH) {
 // CPU path: extract trigger region from center of aimbot buffer
 scanData = aimData;
 offX = (aimW - trigW) / 2;
 offY = (aimH - trigH) / 2;
} else {
 // GPU path: no buffer available, do separate capture
 CaptureRegionAdaptive(...)
}
```

**CPU path advantage:** Zero additional DXGI calls. The triggerbot reads from the same frame the aimbot already captured. No extra GPU work.

**GPU path drawback:** Requires a separate `CaptureRegionAdaptive` call which may timeout if the GPU path already acquired the frame. This is why triggerbot is less reliable in GPU mode.

### LButton Guard

```cpp
if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return;
```

Prevents the triggerbot from firing while you're manually holding left click. Without this, the triggerbot would attempt to click mid-spray, creating button state conflicts.

---

## 4. Assist (Magnet) - Detailed

### Toggle Logic

```cpp
bool key_down = GetAsyncKeyState(cfg::assist_apply_deltakey) & 0x8000;
if (key_down && !keyPressProcessed) {
 key_ativa = !key_ativa; // toggle state
 keyPressProcessed = true;
}
if (!key_down) keyPressProcessed = false;
```

This is edge-triggered toggle, not hold. State persists until toggled again.

### Formula

Identical to `apply_delta()`:

```cpp
exact_x = (deltaX / smooth) * assist_speed + overflow_x;
exact_y = (deltaY / smooth) * assist_speed + overflow_y;
```

Uses `cfg::apply_deltaassist_smooth` and `cfg::assist_speed` instead of the aimbot's values.

### FOV Contribution to MAX-FOV (post-2026-05-31)

Assist no longer writes `currentFOV`. Instead, `cfg::apply_deltaassist_fov`
contributes to `ComputeMaxFov()` - the capture region grows to include
assist's FOV when assist is active. Default 1 px gives almost nothing
back. **Increase this if you want assist to operate over a meaningful
area.**

---

## 5. Flicker (nonmode_a) - Detailed

### Execution Path

Same as mode_a but calls `SnapShoot_F()` instead of `SnapShoot_P()`.

### SnapShoot_F Formula

Identical to SnapShoot_P but uses `cfg::nonmode_a_distance`:

```cpp
float mult = cfg::nonmode_a_distance > 0.0f ? cfg::nonmode_a_distance : 1.0f;
```

Sends `'F'` prefix instead of `'P'`.

### Arduino F vs P Behavior

| Command | Report 1 | Report 2 |
|---------|----------|----------|
| P (Silent) | `report(real\|LEFT, x, y)` | `report(real, -x, -y)` <- snapback |
| F (Flicker) | `report(real\|LEFT, x, y)` | `report(real, 0, 0)` <- NO snapback |

Flicker moves to target and clicks, but your crosshair stays at the new position. Silent aim snaps back to original.

### Data Source (post-2026-05-31)

Flicker reads its OWN per-mode coords `nonmode_a_x/y` populated by the
capture thread using `nonmode_a_fov` as the FOV mask:

```cpp
// In nonmode_a() thread:
int mx = nonmode_a_x;
int my = nonmode_a_y;
if (mx == 0 && my == 0) {
 WaitForFreshCapture(seqBefore, FRESH_TIMEOUT_MS,
 nonmode_a_x, nonmode_a_y, mx, my);
}
if (mx != 0 || my != 0) {
 SnapShoot_F(mx, my);
}
```

Flicker now uses the **silent-aim offsets** (`mode_a_target_offset_x/y`)
applied by the capture thread when populating `nonmode_a_x/y` - NOT
aimbot's `target_offset_*`. This matches legacy behaviour where
`nonmode_a` was a "second silent aim with different FW behaviour."

When `head_anchor_proportional` is enabled, flicker also benefits from
`RefineHeadAnchor` - same shoulder-band X + proportional Y as silent.

---

## 5.5. Head Anchor Refinement (NEW 2026-05-31)

Ported from `tfirm` reference. Affects silent + flicker when
`cfg::mode_a_head_targeting` is on. Adds three refinements on top of the
bare topmost-Y anchor.

### Variables

| Variable | Default | Range | Purpose |
|---|---|---|---|
| `head_anchor_proportional` | true | bool | Master enable |
| `head_anchor_band_rows` | 0 | 0-20 | Top-band rows averaged (0 = auto: clamp(height/4, 2, 6)) |
| `head_anchor_gap_tolerance` | 2 | 0-10 | Non-purple rows allowed in walk-down |
| `head_anchor_close_pct` | 18 | 0-50 | Y offset % for close targets |
| `head_anchor_mid_pct` | 10 | 0-50 | Y offset % for mid targets |
| `head_anchor_close_min_h` | 30 | 5-200 | Min height (px) to be "close" |
| `head_anchor_mid_min_h` | 10 | 1-100 | Min height (px) to be "mid" |

### Algorithm

```cpp
static bool RefineHeadAnchor(const BYTE* data, int bufW, int bufH, int stride,
 const ModeCandidate& cand, int modeHalfFov,
 int& outDx, int& outDy)
{
 if (!cand.found) return false;
 if (!cfg::head_anchor_proportional) {
 outDx = cand.bestTDx; // raw topmost-Y delta
 outDy = cand.bestTDy;
 return true;
 }

 // Step 1: walk down topmost column to estimate height
 int gap = 0, botY = topY;
 for (y = topY+1; y < bufH; ++y) {
 if (IsTargetColor(pixel at (topX, y))) {
 botY = y; gap = 0;
 } else if (++gap > head_anchor_gap_tolerance) break;
 }
 int height = botY - topY + 1;

 // Step 2: shoulder-band X averaging
 int bandRows = (head_anchor_band_rows > 0) ? head_anchor_band_rows
 : clamp(height/4, 2, 6);
 long long sumX = 0; int n = 0;
 for (y in [topY, min(bufH, topY + bandRows)]):
 for (x in [halfW - modeHalfFov, halfW + modeHalfFov]):
 if (IsTargetColor(pixel at (x, y))) {
 sumX += x; n++;
 }
 int anchorX = (n > 0) ? sumX / n : topX;

 // Step 3: proportional Y offset
 int headOff = 0;
 if (height >= close_min_h) headOff = max(3, height * close_pct / 100);
 else if (height >= mid_min_h) headOff = max(1, height * mid_pct / 100);

 outDx = anchorX - halfW;
 outDy = topY - halfH + headOff;
 return true;
}
```

### Why Shoulder-Band X?

Topmost-Y pixel's X may be one shoulder when the outline is partial. At
range, the top 2-6 rows contain TWO shoulder blobs; their midpoint sits
dead-centre under the head. The average X gives the correct head anchor.

### Why Proportional Y?

Fixed-pixel Y offset breaks at range extremes:
- Tall close enemy (60 px tall): offset 3 -> lands too high (forehead OK
 but not eyes). Want ~11 px (18%) -> forehead/eyes.
- Tiny far enemy (8 px tall): offset 3 -> lands BELOW the entire enemy
 (head is only 4 px tall). Want 0 -> aim crown directly.

Proportional offset adapts: bigger enemy -> bigger offset; tiny enemy -> 0.

### Cost

Per frame, per mode (silent + flicker both run RefineHeadAnchor):
- Walk-down: ≤ enemy_height pixel reads (~50 on close target).
- Band scan: bandRows × (2·modeHalfFov + 1) pixel reads (~480 at 80×80 FOV
 with band_rows=6).
- Total per mode: ~530 LUT lookups -> ~10 µs.
- Both modes: ~20 µs per frame.

Negligible.

### Tuning Recipe

| Symptom | Adjust |
|---|---|
| Shots land slightly above heads on close targets | Lower `close_pct` (try 12-15) |
| Shots land in necks on close targets | Raise `close_pct` (try 22-25) |
| Shots miss above tiny far targets | Raise `mid_min_h` (try 15) or lower `mid_pct` (try 6) |
| Outline is jagged, walk-down stops early | Raise `gap_tolerance` (try 3-4) |
| Multiple stacked enemies confuse the band | Force `band_rows = 2` (less averaging, more "head-only") |

---

## 5.6. Trigger Polygon Check (NEW 2026-05-31)

`cfg::trigger_polygon_check` (default true) selects the trigger mode:

### Polygon Mode (default)

```cpp
// 4-ray crossing test
bool hitPlusX = false, hitMinusX = false, hitPlusY = false, hitMinusY = false;
for (r = 1; r <= maxRadius; ++r) {
 if (matchAt(centerX + r, centerY)) hitPlusX = true;
 if (matchAt(centerX - r, centerY)) hitMinusX = true;
 if (matchAt(centerX, centerY + r)) hitPlusY = true;
 if (matchAt(centerX, centerY - r)) hitMinusY = true;
 if (hitPlusX && hitMinusX && hitPlusY && hitMinusY) break;
}
if (hitPlusX && hitMinusX && hitPlusY && hitMinusY) {
 sendCommand(0, 0, 'L'); // fire
}
```

Crosshair is "inside" a purple region iff ALL FOUR cardinal rays hit
purple within trigger FOV. Rejects:
- Single-sided UI purples (HP bar, ability icon, muzzle flash - only 1
 or 2 rays hit).
- Crosshair just outside enemy outline (away-side rays escape into empty
 space).

Short-circuits once all four rays hit, so expected cost on a real
"inside enemy" frame is ~2-4 pixel probes.

### Legacy Mode (`trigger_polygon_check = false`)

Spiral-first-hit: fires on ANY purple pixel within trigger FOV. Faster
but noisier (false fires on isolated UI purples).

### Trade-off

Polygon check is the default because in Valorant most false fires come
from single-sided UI elements (purple skill icons, HP bars, etc.). It
slightly reduces fire rate on partial outlines (only 1-3 rays hit
through a thin outline) but the precision gain is worth it.

---

## 5.7. Distance-Aware Aimbot Smoothing (NEW 2026-05-31)

`cfg::apply_delta_dist_smoothing` (default true) scales aimbot's
per-frame output by `dist_factor` based on `|delta|`:

| Condition | dist_factor | Default |
|---|---|---|
| `|delta|² < apply_delta_near_dist²` | `apply_delta_near_mult` | 0.4 |
| `|delta|² < apply_delta_mid_dist²` | `apply_delta_mid_mult` | 0.7 |
| else | 1.0 | (no scaling) |

### Why Squared Distance

Skips the sqrt. Compares against squared thresholds instead. Equivalent
math, ~5 cycles cheaper per frame.

### Effect

Close target (`|delta|` < 10): aimbot moves 40% of normal -> finer
micro-corrections, less overshoot near target.

Mid target (`|delta|` 10-30): aimbot moves 70% -> smoother tracking
during pursuit.

Far target (`|delta|` ≥ 30): aimbot moves at full speed -> snaps toward
target quickly.

Without this, aimbot would oscillate around the target ("buzz") when
very close because each frame's full-speed move overshoots.

### Affects ONLY apply_delta

Magnet (assist) does NOT use distance-aware scaling. SnapShoot_P/F
(silent/flicker) are one-shot - no smoothing.

### Tuning

If aimbot oscillates: lower `near_mult` (try 0.25) and `mid_mult` (try 0.55).
If aimbot lags into close range: raise `near_dist` (try 15) and `near_mult` (try 0.55).
If aimbot is too slow at range: should not happen since `dist >= mid_dist -> 1.0`.
If you want pure smoothing without distance behaviour: disable the
master toggle.

---

## 6. Color Detection - Detailed

### LUT Construction

The 16MB `std::array<bool, 16777216>` is indexed by `R * 65536 + G * 256 + B`. Each entry is precomputed:

```
For every (R, G, B) in [0,255]³:
 pass_rgb = menorRGB[0] <= R <= maiorRGB[0]
 && menorRGB[1] <= G <= maiorRGB[1]
 && menorRGB[2] <= B <= maiorRGB[2]

 if useIstrigFilter:
 convert (R,G,B) to (H,S,V) using integer math
 pass_hsv = menorHSV[0] <= H <= maiorHSV[0]
 && menorHSV[1] <= S <= maiorHSV[1]
 && menorHSV[2] <= V <= maiorHSV[2]
 // Red hue wraps: if hue range crosses 0°, check (H <= max || H >= min)
 LUT[index] = pass_rgb && pass_hsv
 else:
 LUT[index] = pass_rgb
```

Build time: ~50ms on modern CPU. Triggered when `color_mode` changes or `useIstrigFilter` toggles.

### Integer HSV Conversion

```
max = max(R, G, B)
min = min(R, G, B)
delta = max - min

V = max * 100 / 255 [0-100]
S = (delta * 100) / max [0-100], 0 if max == 0

H (degrees 0-360):
 if R is max: H = 60 * (G - B) / delta
 if G is max: H = 120 + 60 * (B - R) / delta
 if B is max: H = 240 + 60 * (R - G) / delta
 if H < 0: H += 360
```

No floating point. All integer division.

### Color Mode RGB/HSV Ranges

| Mode | RGB Min | RGB Max | HSV Min | HSV Max |
|------|---------|---------|---------|---------|
| 0 (Purple) | (70, 0, 120) | (255, 190, 255) | (270, 38, 40) | (310, 100, 100) |
| 1 (Anti-Purple) | (70, 110, 120) | (255, 190, 255) | (270, 25, 40) | (310, 100, 100) |
| 2 (Yellow) | (168, 168, 0) | (255, 255, 110) | (55, 5, 70) | (65, 100, 100) |
| 3 (Red) | (225, 45, 45) | (255, 136, 136) | (0, 37, 88) | (1, 80, 100) |

**Anti-Purple vs Purple:** Anti-Purple has a higher green minimum (110 vs 0), which rejects dark purples that might match UI elements or shadows.

**Red hue detection:** HSV range (0, 37, 88)-(1, 80, 100) uses the hue wrap check: `H <= 30 || H >= 330`. This correctly handles red hues that straddle the 0°/360° boundary.

### Spiral Search

The spiral searches outward from the center of the capture region, ring by ring:

```
for radius = 0 to max_radius:
 scan top edge of ring (y = center - radius)
 scan bottom edge of ring (y = center + radius)
 scan left edge of ring (x = center - radius), excluding corners
 scan right edge of ring (x = center + radius), excluding corners

 if found && radius² > bestDist²:
 break // no closer pixel possible in outer rings
```

**Early exit:** Once a matching pixel is found and the current ring's minimum possible distance (radius²) exceeds the best found distance, the search stops. This means close targets are found in microseconds.

**Worst case:** No matching pixel - scans entire FOV region. For FOV 100, that's 10,000 pixels × 1 LUT lookup each ≈ 50-100µs.

---

## 7. GPU Compute Path - Detailed

### Limitations

| Limitation | Value | Reason |
|------------|-------|--------|
| Max FOV per axis | 255 | Pixel coords packed in 8 bits: `(dist2 << 16) \| (y << 8) \| x` |
| LUT upload | 16MB 3D texture | `R8_UNORM` format, 256³ voxels |
| Readback | 4 bytes | Single uint32 result buffer |
| Fallback | Automatic | If FOV > 255 or GPU init fails, falls back to CPU |

### Compute Shader Logic

```hlsl
// Per-pixel thread:
float4 pixel = captureTexture[threadId.xy];
float lutValue = lutTexture3D[uint3(pixel.r*255, pixel.g*255, pixel.b*255)];

if (lutValue > 0.5) {
 int dx = threadId.x - centerX;
 int dy = threadId.y - centerY;
 int dist2 = dx*dx + dy*dy;
 uint packed = (dist2 << 16) | (threadId.y << 8) | threadId.x;
 InterlockedMin(resultBuffer[0], packed);
}
```

`InterlockedMin` ensures the closest pixel wins when multiple threads find matches simultaneously.

### When to Use GPU

| Scenario | Recommendation |
|----------|---------------|
| FOV ≤ 255 + discrete GPU | GPU may help |
| FOV > 255 | CPU only (auto-fallback) |
| Integrated GPU | CPU is likely faster |
| High CPU load from other apps | GPU offloads detection |
| GPU already at high utilization | CPU to avoid contention |

---

## 8. Interaction Matrix

Which settings affect which features:

| Setting | Aimbot | Silent | Flicker | Trigger | Assist |
|---------|--------|--------|---------|---------|--------|
| `apply_delta_fov` | ✓ filter + MAX-FOV | | | | |
| `mode_a_fov` | | ✓ filter + MAX-FOV | | | |
| `nonmode_a_fov` | | | ✓ filter + MAX-FOV | | |
| `apply_deltaassist_fov` | | | | | ✓ MAX-FOV only |
| `target_offset_x/y` | ✓ aim point | | | | (reads aimbot's) |
| `mode_a_target_offset_x/y` | | ✓ aim point | ✓ aim point | | |
| `color_mode` | ✓ LUT | ✓ LUT | ✓ LUT | ✓ (fixed color + sens) | ✓ LUT |
| `useIstrigFilter` | ✓ LUT | ✓ LUT | ✓ LUT | ✗ | ✓ LUT |
| `distance` | | ✓ multiplier | | | |
| `nonmode_a_distance` | | | ✓ multiplier | | |
| `apply_delta_smooth` | ✓ | | | | |
| `speed` | ✓ | | | | |
| `apply_delta_dist_smoothing` + tiers | ✓ | | | | |
| `apply_deltaassist_smooth` | | | | | ✓ |
| `assist_speed` | | | | | ✓ |
| `mode_a_head_targeting` | | ✓ anchor | ✓ anchor | | |
| `head_anchor_proportional` + 6 tiers | | ✓ anchor | ✓ anchor | | |
| `mode_a_cooldown_ms` | | ✓ Arduino | | | |
| `dead_body_filter` + threshold | | ✓ suppress | | | |
| `trigger_polygon_check` | | | | ✓ algorithm | |
| `min_cluster_size` | ✓ per-mode | ✓ per-mode | ✓ per-mode | | ✓ per-mode |

**Key insights**:

1. **Per-mode coords** (post-2026-05-31): each mode reads its OWN coord
 pair. Silent reads `mode_a_x/y`, flicker reads `nonmode_a_x/y`,
 aimbot reads `apply_delta_x/y`. No sharing, no race.
2. **All FOV variables** contribute to `ComputeMaxFov()` which sets the
 capture region size. Each mode's coords are also filtered by their
 OWN half-FOV inside `FindTargets`.
3. **Flicker uses silent's offsets** (`mode_a_target_offset_x/y`) - set
 by capture thread when populating `nonmode_a_x/y`. NOT aimbot's offsets.
4. **Head targeting + proportional refinement** is shared between
 silent + flicker.
5. **Cluster validation is per-mode** - a noisy detection in one mode no
 longer blanks the others.
6. **Distance-aware smoothing applies ONLY to aimbot** (apply_delta).
 Magnet/SnapShoot_P/F do not use it.
7. **Trigger uses its own fixed color reference** + tolerance (no LUT).

---

## 9. Timing Chain Analysis

### Frame-to-Action Latency

```
DXGI AcquireNextFrame [0ms - blocks until frame ready]
 v (GPU -> CPU copy) [~0.5ms - CopySubresourceRegion + Map]
Spiral search [~0.05ms - with early exit for close targets]
apply_delta computation [~0.001ms]
UDP send [~0.01ms - non-blocking, fire-and-forget]
 v (network) [~0.1ms - ethernet, LAN]
Arduino parse + exec [~0.01ms]
USB HID report [~1ms - 1ms polling interval]
 v (USB -> OS) [~0.125ms - USB poll at 8kHz on some controllers]
OS processes input [~1ms]
Game reads input [next game frame - up to 16.6ms @ 60fps]
 --------------------
Total best case: ~3ms (game at high fps)
Total worst case: ~20ms (game at 60fps, unlucky frame timing)
```

### Silent Aim Specific

The mode_a thread polls at 1ms intervals. In the worst case, it takes up to 1ms to detect the keypress after the physical press. Add this to the above chain.

The 100ms debounce adds no latency to the first press - it only prevents re-triggers within 100ms of the last fire.

---

## 10. Calibration Procedures

### Silent Aim Distance - Precision Method

**Setup:** Practice range, static target, aimbot OFF, silent aim ON.

**Step 1 - Coarse:**
```
Set distance = 1.0
Fire at target 10px from center
Observe: shot lands ~4px from target (undershooting)
-> distance needed ≈ 10 / (10 - 4) × 1.0 ≈ 1.67
```

Wait, simpler: `moveX = deltaX × distance`, so:
```
If you need the shot to travel 10 pixels and it traveled 4:
ratio = 10 / 4 = 2.5
New distance = old_distance × ratio = 1.0 × 2.5 = 2.5
```

**Step 2 - Fine:**
```
Set distance = 2.5
Fire at 10px offset -> lands 1px short
ratio = 10 / 9 = 1.11
New distance = 2.5 × 1.11 = 2.78
```

**Step 3 - Verify:** Test at 3px, 8px, 15px offsets. All should hit equally well since the formula is linear.

### FOV - Finding Your Sweet Spot

**For aimbot:**
1. Start at FOV 50
2. Play normally - if the aimbot doesn't activate often enough, increase by 10
3. If it locks onto wrong targets or looks unnatural, decrease by 10
4. Sweet spot is usually 60-100

**For silent aim:**
1. Start at FOV 60
2. If you're getting body shots (detection finding body pixels), decrease to 40-50
3. If it doesn't fire when it should, increase to 70-80
4. Remember: smaller FOV = detection only finds pixels near crosshair = more likely head
