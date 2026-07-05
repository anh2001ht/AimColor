# Kiến trúc Coloruino

> **Trạng thái**: Tài liệu này phản ánh kiến trúc **MAX-FOV + per-mode filter**
> (sau refactor 2026-05-31). Mô hình lịch sử xem trong CHANGELOG.

## Sơ đồ hệ thống

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
| | | modes' FOVs) | | Frame(1ms) | | 3x candidate | | |
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
| Capture | High (`set_thread_high_priority`) | DXGI `AcquireNextFrame(1ms)` kernel-block tới vblank | **Ghi**: `currentFOV`, `apply_delta_x/y`, `mode_a_x/y`, `nonmode_a_x/y`, `oX/oY`, `capture_seq`. Đọc `cfg::*` live mỗi iter. |
| mode_a | `THREAD_PRIORITY_HIGHEST` | polling `sleep_for(1ms)` | Đọc: `mode_a_x/y`, `capture_seq`. |
| nonmode_a | `THREAD_PRIORITY_HIGHEST` | polling `sleep_for(1ms)` | Đọc: `nonmode_a_x/y`, `capture_seq`. |
| WebServer | Normal | `accept()` block | Đọc/ghi giá trị `cfg::*`. Lưu config. |
| AntiDebug | Normal | Loop liên tục | Đọc system state. Gọi `KillSelf()` khi detect. |
| Main | Normal | keepalive `sleep_for(1hr)` | Chỉ setup |

### Ownership `currentFOV` (sau refactor)

**Cũ (trước 2026-05-31)**: Nhiều thread ghi `currentFOV` cạnh tranh
(aimbot, assist, silent, flicker). Gây FOV race -> silent aim choke.

**Mới**: Capture thread là **writer duy nhất**. Tính mỗi iter qua
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

Toggle Web-UI được nhận ở **capture iteration kế tiếp** - không cần signalling.

`fovMutex` vẫn còn cho store của capture-thread (`currentFOV = w`) - giữ để tương thích legacy với bất kỳ read-side code path nào vẫn có thể dùng nó.

---

## Capture Loop (FindTargets per-mode)

Một linear scan duy nhất trên buffer MAX-FOV, mask từng candidate theo box FOV riêng của mode:

```
for each row y in [0, h):
 skip row if |dy| > max(aimbotHalf, silentHalf, flickerHalf)

 for each col x in [0, w):
 if !IsTargetColor(px): continue

 compute dx, dy, dist^2

 if active(aimbot) && |dx| <= aimbotHalf && |dy| <= aimbotHalf:
 update aimbot's closest + topmost
 if active(silent) && |dx| <= silentHalf && |dy| <= silentHalf:
 update silent's closest + topmost
 if active(flicker) && |dx| <= flickerHalf && |dy| <= flickerHalf:
 update flicker's closest + topmost
```

Chi phí: O(W*H), W=H=MAX FOV. Ở 100x100 điển hình với match màu tím thưa,
~10k LUT lookup + 3 branch nhẹ mỗi hit. Dưới mili giây.

Struct `ModeCandidate` theo mode:

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

## Post-Processing theo Mode

Sau `FindTargets`, `OptimizedProcessImage` chạy ba chuỗi xử lý độc lập:

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

`CountNeighbours` được gọi **theo mode**: mode có closest pixel với
< `cfg::min_cluster_size` neighbor tím sẽ bị drop mà không ảnh hưởng mode khác.
Chi phí 8 LUT lookup x 3 mode = 24 - không đáng kể.

---

## RefineHeadAnchor (port Tier 1 từ tfirm)

Ba bước trên buffer cho bất kỳ mode nào tìm thấy target:

### Step 1: Walk-Down (ước lượng height)

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

Đi xuống theo cột của topmost pixel, cho phép tối đa N row non-purple liên tiếp
(xử lý outline đứt / hair gap).

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

Average X trên N row tím đầu trong FOV của mode. Ở xa, band bắt cả hai shoulder blob
-> midpoint = body centre ngay dưới đầu.

### Step 3: Proportional Y Offset

```
head_off = 0
if height >= head_anchor_close_min_h:
 head_off = max(3, height * head_anchor_close_pct / 100)
elif height >= head_anchor_mid_min_h:
 head_off = max(1, height * head_anchor_mid_pct / 100)
// else tiny target - leave head_off = 0 (aim crown)

aim_dx = anchor_x - halfW (back to delta)
aim_dy = start_y - halfH + head_off
```

Tự thích nghi theo khoảng cách địch. Địch gần cao được offset lớn hơn về trán;
địch xa nhỏ nhận 0 để không overshoot lên trên đầu.

---

## Silent Aim Fast/Slow Path

`mode_a()` và `nonmode_a()` dùng cùng pattern:

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

- Loop qua nhiều seq advance, không chỉ một.
- Thoát khi coord khác zero VÀ seq > seqBefore (snapshot value atomically với acquire-load).
- Trả false (với `outX = outY = 0`) khi timeout.
- Timeout = `ComputeFreshTimeoutMs()` = `(2 x 1000 / refresh) + 2 ms`
 floored ở 6 ms.

### Vì sao 2 frame

Capture iter đầu tiên sau key press có thể đã **in-flight** khi key chạm -
`AcquireNextFrame` của nó đã lấy desktop frame render trước khi target xuất hiện.
Iter thứ hai là iter đầu hoàn toàn bắt đầu sau press. 2 frame bắt cả hai trường hợp
mà vẫn dưới ngưỡng cảm nhận của người.

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
ComputeMaxFov() -> w (must be <= 255 due to packing)
AcquireNextFrame(1ms)
 -> CopySubresourceRegion to gpuCaptureTex (SRV-bindable)
 -> Dispatch compute shader (cs_5_0)
 -> Readback 4-byte result buffer
 -> Decode: (dist^2 << 16) | (y << 8) | x
 -> ProcessGPUResult - filters single result per-mode by each mode's FOV
 -> publish seq + run downstream
 -> On GPU failure (FOV > 255): fall back to CPU path
```

**Giới hạn GPU**:
- MAX FOV 255x255 (8-bit packing).
- Single closest-pixel result - không có per-mode candidate, không topmost-Y tracking.
- Cảnh nhiều target mà overall-closest pixel nằm ngoài FOV của aimbot sẽ để aimbot 0,0
 dù một target khác ĐANG nằm trong FOV của nó.
- Default OFF.

### Compute Shader (HLSL CS 5.0)

Mỗi thread:
1. Sample capture texture SRV tại `threadId.xy`.
2. Lookup `(R, G, B)` trong 3D LUT 256^3 (R8_UNORM).
3. Nếu match: `dist^2 = dx^2 + dy^2`; `InterlockedMin(result, packed)`.

---

## Color Detection Pipeline

```
Pixel RGB
 |
 +--> RGB Range Check: menorRGB[i] <= channel <= maiorRGB[i]
 |
 +--> HSV Range Check (if useIstrigFilter):
 |
 +-- Integer HSV conversion (no float):
 | max = max(R,G,B), min = min(R,G,B)
 | S = (max-min) * 100 / max
 | V = max * 100 / 255
 | H = 60 * sector + offset (degrees 0-360)
 |
 +-- Range: menorHSV[i] <= channel <= maiorHSV[i]
 Special case: Red hue wraps (h <= 30 || h >= 330)

Both must pass -> LUT[R*65536 + G*256 + B] = true
```

LUT rebuild mỗi khi color mode hoặc filter toggle đổi. 16 MB build trong ~50 ms.

---

## Triggerbot (Otrigger_action)

Nằm cuối capture loop. Dùng cùng captured buffer.

### Hai mode

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

4-ray crossing test. Loại:
- UI tím một phía (HP bar, ability icon).
- Crosshair nằm ngay ngoài enemy outline (ray phía xa thoát ra vùng trống).

#### Legacy (`cfg::trigger_polygon_check = false`)

Spiral-first-hit: bắn với bất kỳ pixel tím nào trong trigger FOV.

### Color Match

Cả hai mode dùng cùng check tolerance từng channel với reference color của color-mode active:

```cpp
abs(px[2] - targetR) < pixel_sens &&
abs(px[1] - targetG) < pixel_sens &&
abs(px[0] - targetB) < pixel_sens
```

`pixel_sens = 90` (hardcoded). Reference color lấy từ switch
`cfg::color_mode` (ví dụ mode 0 = `RGB(235, 105, 254)`).

### Guards

- `cfg::trigger_action_ativo` enabled.
- `cfg::trigger_action_key` held.
- Flag `has_shot` (một shot mỗi lần giữ key).
- `VK_LBUTTON` KHÔNG held (ngăn conflict với click tay).

---

## Hệ thống smoothing

### apply_delta (Aimbot)

Từng frame với overflow accumulation + optional distance scaling:

```cpp
// Distance-aware multiplier
dist_factor = 1.0;
if (apply_delta_dist_smoothing) {
 dist^2 = deltaX^2 + deltaY^2
 if dist^2 < near_dist^2 -> dist_factor = near_mult (default 0.4)
 elif dist^2 < mid_dist^2 -> dist_factor = mid_mult (default 0.7)
}

// Overflow accumulation
exact_x = (deltaX / smooth) * speed * dist_factor + overflow_x
exact_y = (deltaY / smooth) * speed * dist_factor + overflow_y

moveX = int(exact_x)
moveY = int(exact_y)

overflow_x = exact_x - moveX // carry fractional part
overflow_y = exact_y - moveY
```

Overflow reset về 0 khi:
- Feature disabled
- Nhả activation key
- Delta là (0, 0)

### Magnet (Assist)

Cùng cấu trúc nhưng dùng `apply_deltaassist_smooth` và `assist_speed`.
KHÔNG có distance-aware scaling. Đọc `apply_delta_x/y` (track target của aimbot).

### SnapShoot_P (Silent)

```cpp
moveX = deltaX * cfg::distance
moveY = deltaY * cfg::distance
sendCommand(moveX, moveY, 'P')
```

Linear gain. Không smoothing, không overflow.

### SnapShoot_F (Flicker)

Formula giống hệt với `cfg::nonmode_a_distance`, prefix `'F'`.

---

## Security Layers

### Compile-Time
- `xorstr_()` - mọi string user-visible encrypt ở compile time,
 decrypt lên stack khi runtime.
- VMProtect SDK markers: `VMProtectBeginUltra`, `VMProtectBeginMutation`,
 `VMProtectEnd`.
- FNV-1a compile-time hash để so sánh license key (không plaintext key trong binary).

### Runtime
- HWID binding - config file khóa vào phần cứng cụ thể.
- Anti-debug thread - monitoring liên tục (PEB, DebugPort,
 DebugObject, RemoteDebugger, HW breakpoints, heap flags, RDTSC).
- HTTP auth - panel cấu hình web cần Basic auth.
- Config encryption - XOR cipher trên file `data`.

### Network
- COM firewall rule - tạo âm thầm qua `INetFwPolicy2`, không thấy child process
 `netsh.exe` trong Process Monitor.
- Firewall rule tên "AMD Radeon Software Helper" (khớp binary metadata).
- WebUI port: 13548 (trên vùng well-known service map, ít collision).
- Outbound UDP tới Arduino: dạng DNS trên port 5353 (port mDNS).
- UDP non-blocking - không connection state để detect.

### Disguise
- Binary metadata (FileDescription, ProductName, CompanyName): AMD Radeon Software.
- Firewall rule display name: AMD Radeon Software Helper.
- Web UI title và brand label: "Spotify Web Player" - intentional split-identity
 (cover thị giác cho người liếc qua vai, trong khi binary bên dưới vẫn AMD-branded
 cho consistency với forensic tool).
- Favicon: Spotify SVG màu xanh (inline base64).
- Signed: Authenticode self-signed cert với subject `CN=AMD Radeon Software`.
