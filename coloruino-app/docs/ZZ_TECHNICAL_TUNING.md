# Coloruino Technical Tuning Reference

Tài liệu đào sâu từng biến cấu hình - toán học, tương tác, edge case và code path chính xác.

> **Ghi chú kiến trúc (sau 2026-05-31)**: Kích thước vùng capture hiện là
> **MAX của FOV mọi mode active** (tính mỗi iter bởi `ComputeMaxFov()`).
> Coord của từng mode được filter bằng half-FOV RIÊNG trong `FindTargets`.
> Race kiểu legacy "mode ghi currentFOV" không còn.

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
| `apply_delta_dist_smoothing` | bool | 0/1 | 1 | Aimbot (**MỚI 2026-05-31**) |
| `apply_delta_near_dist` | int | 1-80 | 10 | Aimbot (**MỚI**) |
| `apply_delta_mid_dist` | int | 5-200 | 30 | Aimbot (**MỚI**) |
| `apply_delta_near_mult` | float | 0.05-2.0 | 0.4 | Aimbot (**MỚI**) |
| `apply_delta_mid_mult` | float | 0.05-2.0 | 0.7 | Aimbot (**MỚI**) |
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
| `trigger_polygon_check` | bool | 0/1 | 1 | Triggerbot (**MỚI 2026-05-31**) |
| `mode_a_head_targeting` | bool | 0/1 | 1 | Silent Aim |
| `mode_a_cooldown_ms` | int | 50-500 | 50 | Silent Aim |
| `head_anchor_proportional` | bool | 0/1 | 1 | Silent + Flicker (**MỚI 2026-05-31**) |
| `head_anchor_band_rows` | int | 0-20 | 0 (auto) | Silent + Flicker (**MỚI**) |
| `head_anchor_gap_tolerance` | int | 0-10 | 2 | Silent + Flicker (**MỚI**) |
| `head_anchor_close_pct` | int | 0-50 | 18 | Silent + Flicker (**MỚI**) |
| `head_anchor_mid_pct` | int | 0-50 | 10 | Silent + Flicker (**MỚI**) |
| `head_anchor_close_min_h` | int | 5-200 | 30 | Silent + Flicker (**MỚI**) |
| `head_anchor_mid_min_h` | int | 1-100 | 10 | Silent + Flicker (**MỚI**) |
| `color_mode` | int | 0-3 | 0 | Color |
| `useIstrigFilter` | bool | 0/1 | 1 | Color |
| `use_gpu_processing` | bool | 0/1 | 0 | Performance |
| `dead_body_filter` | bool | 0/1 | 0 | Filtering |
| `dead_body_threshold` | int | 3-60 | 15 | Filtering |
| `min_cluster_size` | int | 0-8 | 2 | Filtering |

> **Đã bỏ trong firmware v2** (không còn trong danh sách): `mode_a_split_delay_us`.
> Lệnh P dùng chuỗi 4-report deterministic, không có artificial delay giữa report.

---

## 1. Aimbot (apply_delta) - Chi tiết

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

### Công thức smoothing của apply_delta()

```cpp
// Distance-aware multiplier (NEW 2026-05-31)
dist_factor = 1.0;
if (cfg::apply_delta_dist_smoothing) {
 dist^2 = deltaX^2 + deltaY^2;
 if (dist^2 < near_dist^2) dist_factor = near_mult; // default 0.4
 else if (dist^2 < mid_dist^2) dist_factor = mid_mult; // default 0.7
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

**Thuộc tính chính:**
- Overflow accumulation đảm bảo movement sub-pixel không bị mất. Movement 0.3px/frame tích lại thành 1px sau khoảng 3 frame.
- `int()` truncate toward zero, không round. Nghĩa là movement hơi thiên về undershoot.
- Overflow reset về 0 khi: feature disabled, key released, hoặc delta là (0,0).

### Movement hiệu dụng mỗi frame

Với target ở pixel offset `(dx, dy)`:

```
movement_per_frame = pixel_offset x (speed / smooth)
frames_to_reach = 1 / (speed / smooth) = smooth / speed
```

| Smooth | Speed | Effective Rate | Frames to Reach |
|--------|-------|---------------|-----------------|
| 1.0 | 1.0 | 100% per frame | 1 |
| 1.4 | 0.4 | 28.6% per frame | ~3.5 |
| 2.0 | 0.8 | 40% per frame | ~2.5 |
| 3.0 | 0.3 | 10% per frame | ~10 |
| 1.0 | 0.1 | 10% per frame | ~10 |

Lưu ý: "frames to reach" là xấp xỉ - delta nhỏ dần mỗi frame khi tiến gần target,
nên đây là exponential decay, không tuyến tính. Target không bao giờ đạt chính xác trong N frame; nó tiến tiệm cận.

### Tương tác FOV với feature khác (sau 2026-05-31)

**Model cũ (deprecated)**: Mỗi mode ghi `currentFOV` cạnh tranh ->
silent aim FOV race bug -> "choke on first fire."

**Model mới**: Capture thread là **writer duy nhất**. Mỗi iter:

```
w = ComputeMaxFov(); // max of active modes' FOVs
capture region = w x w
FindTargets(..., aimbotHalf=apply_delta_fov/2 if active else 0,
 silentHalf=mode_a_fov/2 if active else 0,
 flickerHalf=nonmode_a_fov/2 if active else 0)
```

Candidate của từng mode được FILTER vào box FOV riêng trong lúc scan.
Coord theo mode (`apply_delta_x/y`, `mode_a_x/y`, `nonmode_a_x/y`) là kết quả
của filtering đó. Không mode nào ghi `currentFOV`.

Thay đổi FOV slider hoặc activation toggle trong Web-UI có hiệu lực ở capture iter kế tiếp
(không cần signalling). Phản hồi live.

**Hiệu ứng thực tế**: Nếu aimbot FOV 62 và silent FOV 80, capture là 80x80.
Aimbot chỉ "thấy" pixel trong +/-31 quanh tâm (mask FOV riêng). Silent thấy toàn vùng 80.
Cả hai chạy đồng thời không can thiệp nhau.

### Activation Keys - OR Logic

```cpp
if (!GetAsyncKeyState(cfg::apply_deltakey1) && !GetAsyncKeyState(cfg::apply_deltakey2)) {
 overflow_x = overflow_y = 0;
 return;
}
```

Bất kỳ key nào cũng activate. Khi không key nào bấm, overflow bị flush (không drift tồn dư).

### target_offset_x / target_offset_y

Áp TRƯỚC khi delta vào `apply_delta()`:

```cpp
apply_delta_x = raw_dx + cfg::target_offset_x;
apply_delta_y = raw_dy + cfg::target_offset_y;
```

`raw_dx` là pixel offset từ tâm vùng capture tới closest matching pixel. `target_offset` dịch điểm aim.

**Coordinate system:**
- X dương = phải
- Y dương = xuống
- raw_dx âm = target bên trái tâm
- raw_dy âm = target phía trên tâm

**Ví dụ:** Pixel outline target tại (-3, -8) tương đối với tâm. Với offset (1, 5):
- apply_delta_x = -3 + 1 = -2 (aim lệch trái 2px)
- apply_delta_y = -8 + 5 = -3 (aim trên tâm 3px)

Offset đưa điểm aim từ cạnh outline vào gần body center.

---

## 2. Silent Aim (mode_a) - Chi tiết

### Execution Path (sau 2026-05-31)

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

`FRESH_TIMEOUT_MS = (2 x 1000 / refresh) + 2`, tối thiểu 6 ms. Xem
`ARCHITECTURE.md` để biết lý do timeout 2-frame.

`WaitForFreshCapture` loop qua nhiều seq advance (không chỉ một) -
bắt trường hợp "target xuất hiện giữa các iter". Snapshot coord atomically
với seq acquire-load (không torn read).

### Formula SnapShoot_P

```cpp
float mult = cfg::distance > 0.0f ? cfg::distance : 1.0f;
int moveX = static_cast<int>(deltaX * mult);
int moveY = static_cast<int>(deltaY * mult);
sendCommand(moveX, moveY, 'P');
```

Code cũ dùng normalize -> clamp(10) -> multiply, tương đương đại số với
`deltaX * distance` trong mọi trường hợp (clamp và normalize triệt tiêu nhau).
Giờ đã đơn giản thành phép nhân trực tiếp.

**Thuộc tính:**
1. `distance` là linear multiplier thuần: `HID_units = pixels x distance`
2. Movement tỉ lệ với pixel offset ở mọi range
3. Không float operation trung gian (bỏ sqrt/normalize)

### `distance` phải bằng gì

Để hit hoàn hảo, HID units gửi đi phải bằng cursor movement cần trong game để bù pixel offset.

```
distance = HID_counts_per_screen_pixel
```

Phụ thuộc:

| Factor | Effect |
|--------|--------|
| In-game sensitivity | Sens thấp -> cần nhiều count hơn mỗi pixel -> distance cao hơn |
| Game's internal sensitivity formula | Mỗi game convert count khác nhau |
| Resolution | Res cao hơn -> nhiều pixel mỗi độ hơn -> ratio khác |
| Game FOV | FOV rộng hơn -> nhiều pixel mỗi độ ở tâm màn hình |
| Windows pointer speed | Nên là 6/11 (1:1). Giá trị khác scale tuyến tính |

**Ước lượng cho Valorant (103 độ HFOV, 1920x1080):**

```
degrees_per_HID_count = sensitivity x 0.07
 = 0.34 x 0.07 = 0.0238°

degrees_per_pixel = horizontal_FOV / horizontal_resolution
 = 103 / 1920 = 0.05365°

distance = degrees_per_pixel / degrees_per_HID_count
 = 0.05365 / 0.0238 ~= 2.25
```

**Lưu ý:** Đây là lý thuyết. Thực tế game có thể áp scaling, rounding hoặc transform phi tuyến bổ sung. Cần calibration thực nghiệm.

### Vì sao Clamp từng tồn tại (lịch sử)

Source UCAimColor gốc cap `dist` ở 10.0 để giới hạn movement magnitude tối đa. Nhưng vì nó chia cho `dist` rồi nhân với `dist x multiplier`, clamp này inert về mặt đại số. Có lẽ ban đầu định giới hạn movement range nhưng cấu trúc formula làm mất tác dụng.

Nếu muốn giới hạn distance thật sự, cần:

```cpp
// To actually limit max movement:
if (dist > MAX_RANGE) return; // skip shot entirely if too far
```

### Head Targeting (`mode_a_head_targeting`)

Khi bật, silent và flicker dùng pixel tím **topmost-Y** làm aim anchor
thay vì pixel closest-to-centre.

```cpp
// In OptimizedProcessImage:
if (silent.found) {
 int aim_dx, aim_dy;
 if (cfg::mode_a_head_targeting) {
 // Topmost-Y, optionally refined by RefineHeadAnchor (see section 6 below).
 RefineHeadAnchor(screenData, w, h, stride, silent, silentHalf,
 aim_dx, aim_dy);
 } else {
 aim_dx = silent.bestCDx; // closest-to-centre
 aim_dy = silent.bestCDy;
 }
 // ... apply offset, dead-body filter, write mode_a_x/y
}
```

Khi `cfg::head_anchor_proportional` CŨNG bật (default), `RefineHeadAnchor`
thêm shoulder-band X averaging + proportional Y offset trên bare topmost-Y pixel -
xem phần 6 bên dưới.

**Tác động lên offset:** Với head targeting ON + proportional refinement ON,
`aim_dy` đã nằm ở trán/mắt (scale theo enemy height). `mode_a_target_offset_y`
nên nhỏ (0-3) vì điểm bắt đầu đã ở trên đầu.

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

Corpse ragdoll gây Y jump lớn giữa frame. Filter suppress silent aim trong frame đó,
ngăn bắn vào xác. Chỉ ảnh hưởng `mode_a_x/y` - aimbot tracking vẫn bình thường.

### Cluster Validation (`min_cluster_size`)

```cpp
if (found && cfg::min_cluster_size > 0) {
 if (CountNeighbours(screenData, w, h, raw_dx, raw_dy) < cfg::min_cluster_size)
 found = false; // reject isolated pixel
}
```

Check 8 pixel lân cận quanh hit của spiral search. Reject single-pixel noise từ UI,
particle effect hoặc artefact DXGI. Chi phí: 8 LUT lookup (data trong L1 cache).

### mode_a_target_offset_x / mode_a_target_offset_y

Áp trong capture thread, tách riêng với aimbot offsets:

```cpp
mode_a_x = aim_dx + cfg::mode_a_target_offset_x;
mode_a_y = aim_dy + cfg::mode_a_target_offset_y;
```

**Range: -100 tới 100** (rộng hơn range 0-20 của aimbot).

Chúng độc lập với `target_offset_x/y` của aimbot. Đổi aimbot offsets KHÔNG ảnh hưởng silent aim, và ngược lại.

### Edge Detection + Debounce

```cpp
if (key_is_down && !key_was_down && (now - lastFireTime) > DEBOUNCE) {
 // fire
 lastFireTime = now;
}
key_was_down = key_is_down;
```

**Vì sao debounce 100ms:** Lệnh P của Arduino gửi hai HID report: move+click,
rồi snapback+release. Snapback tạm thời release mọi nút. Nếu key silent aim là
nút chuột (XButton2), OS thấy nó release trong một USB frame (~1ms), rồi press lại
bởi report chuột thật tiếp theo. Không debounce, thread mode_a sẽ xem đó là keypress edge mới và bắn lại.

**Cooldown phía Arduino:** `P_COOLDOWN` 200ms bổ sung ngăn rapid-fire dù PC gửi nhiều lệnh P nhanh.

### Yếu tố accuracy - xếp theo ảnh hưởng

1. **Calibration `distance`** - sai giá trị = over/undershoot nhất quán
2. **`mode_a_head_targeting`** - khi ON, aim vào đỉnh đầu thay vì pixel body gần nhất
3. **`dead_body_filter`** - ngăn shot phí vào corpse ragdoll
4. **`min_cluster_size`** - reject single-pixel noise gây random misfire
5. **`mode_a_target_offset_y`** - bù cho head targeting landing ở top outline thay vì hitbox center
6. **`mode_a_fov`** - FOV lớn = tăng khả năng detect body pixel ở range
7. **Split delay** - tách move và click thành USB frame riêng có thể cải thiện hit registration
8. **Latency** - ~5-15ms từ detection tới HID report execution; target đang di chuyển sẽ shift
9. **Integer truncation** - `int()` mất tối đa 0.99 mỗi trục; tệ nhất ở delta nhỏ

---

## 3. Triggerbot (trigger_action) - Chi tiết

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

### Color Matching riêng cho Triggerbot

Triggerbot dùng **giá trị màu cố định theo mode**, không dùng LUT:

```cpp
switch (cfg::color_mode) {
 case 0: case 1: pixel_color = RGB(235, 105, 254); break; // purple
 case 2: pixel_color = RGB(255, 255, 85); break; // yellow
 case 3: pixel_color = RGB(254, 99, 106); break; // red
}
```

Với tolerance cố định `pixel_sens = 90` mỗi channel:

```cpp
abs(px.R - targetR) < 90 &&
abs(px.G - targetG) < 90 &&
abs(px.B - targetB) < 90
```

**Điều này khác detection LUT của aimbot.** Triggerbot check một màu reference duy nhất
± 90 mỗi channel, còn aimbot dùng full RGB range + optional HSV filter. Nghĩa là
triggerbot có thể bắn vào pixel hơi khác với pixel aimbot detect.

### trigger_action_fovX / trigger_action_fovY

Vùng scan là `(fovX * 2) x (fovY * 2)` pixel centered trên màn hình:

```cpp
int trigW = cfg::trigger_action_fovX * 2;
int trigH = cfg::trigger_action_fovY * 2;
```

fovX = 3 -> vùng scan rộng 6 pixel. Cố ý rất nhỏ - triggerbot chỉ nên bắn
khi target gần như nằm trên crosshair.

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

**Lợi thế CPU path:** Không thêm DXGI call. Triggerbot đọc cùng frame aimbot đã capture. Không thêm GPU work.

**Nhược điểm GPU path:** Cần call `CaptureRegionAdaptive` riêng, có thể timeout nếu GPU path đã acquire frame. Đây là lý do triggerbot kém ổn định hơn trong GPU mode.

### LButton Guard

```cpp
if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return;
```

Ngăn triggerbot bắn khi bạn đang giữ left click thủ công. Nếu không có, triggerbot sẽ thử click giữa spray, tạo conflict button state.

---

## 4. Assist (Magnet) - Chi tiết

### Toggle Logic

```cpp
bool key_down = GetAsyncKeyState(cfg::assist_apply_deltakey) & 0x8000;
if (key_down && !keyPressProcessed) {
 key_ativa = !key_ativa; // toggle state
 keyPressProcessed = true;
}
if (!key_down) keyPressProcessed = false;
```

Đây là edge-triggered toggle, không phải hold. State giữ tới khi toggle lại.

### Formula

Giống `apply_delta()`:

```cpp
exact_x = (deltaX / smooth) * assist_speed + overflow_x;
exact_y = (deltaY / smooth) * assist_speed + overflow_y;
```

Dùng `cfg::apply_deltaassist_smooth` và `cfg::assist_speed` thay cho giá trị của aimbot.

### Đóng góp FOV vào MAX-FOV (sau 2026-05-31)

Assist không còn ghi `currentFOV`. Thay vào đó, `cfg::apply_deltaassist_fov`
đóng góp vào `ComputeMaxFov()` - vùng capture mở rộng để bao FOV của assist khi assist active.
Default 1 px gần như không cho gì. **Tăng giá trị này nếu muốn assist hoạt động trên vùng có ý nghĩa.**

---

## 5. Flicker (nonmode_a) - Chi tiết

### Execution Path

Giống `mode_a` nhưng gọi `SnapShoot_F()` thay vì `SnapShoot_P()`.

### Formula SnapShoot_F

Giống SnapShoot_P nhưng dùng `cfg::nonmode_a_distance`:

```cpp
float mult = cfg::nonmode_a_distance > 0.0f ? cfg::nonmode_a_distance : 1.0f;
```

Gửi prefix `'F'` thay vì `'P'`.

### Hành vi Arduino F vs P

| Command | Report 1 | Report 2 |
|---------|----------|----------|
| P (Silent) | `report(real\|LEFT, x, y)` | `report(real, -x, -y)` <- snapback |
| F (Flicker) | `report(real\|LEFT, x, y)` | `report(real, 0, 0)` <- KHÔNG snapback |

Flicker di chuyển tới target và click, nhưng crosshair giữ ở vị trí mới. Silent aim snap về ban đầu.

### Data Source (sau 2026-05-31)

Flicker đọc coord theo mode RIÊNG `nonmode_a_x/y` do capture thread populate
bằng `nonmode_a_fov` làm FOV mask:

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

Flicker hiện dùng **offset của silent-aim** (`mode_a_target_offset_x/y`)
do capture thread áp khi populate `nonmode_a_x/y` - KHÔNG phải offset của aimbot.
Khớp legacy behavior nơi `nonmode_a` là "silent aim thứ hai với hành vi FW khác".

Khi `head_anchor_proportional` bật, flicker cũng hưởng lợi từ `RefineHeadAnchor` -
cùng shoulder-band X + proportional Y như silent.

---

## 5.5. Head Anchor Refinement (MỚI 2026-05-31)

Port từ reference `tfirm`. Ảnh hưởng silent + flicker khi
`cfg::mode_a_head_targeting` bật. Thêm ba refinement trên bare topmost-Y anchor.

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

### Vì sao Shoulder-Band X?

X của topmost-Y pixel có thể là một vai khi outline partial. Ở range xa,
2-6 row đầu chứa HAI shoulder blob; midpoint nằm dead-centre dưới đầu. Average X
cho đúng head anchor.

### Vì sao Proportional Y?

Offset Y cố định theo pixel bị hỏng ở các cực range:
- Địch gần cao (60 px): offset 3 -> landing quá cao (trán OK nhưng không tới mắt).
 Muốn ~11 px (18%) -> trán/mắt.
- Địch xa nhỏ (8 px): offset 3 -> landing DƯỚI toàn bộ địch
 (đầu chỉ cao 4 px). Muốn 0 -> aim crown trực tiếp.

Proportional offset thích nghi: địch lớn hơn -> offset lớn hơn; địch nhỏ -> 0.

### Cost

Mỗi frame, mỗi mode (silent + flicker đều chạy RefineHeadAnchor):
- Walk-down: <= enemy_height pixel read (~50 với target gần).
- Band scan: bandRows x (2*modeHalfFov + 1) pixel read (~480 ở FOV 80x80 với band_rows=6).
- Tổng mỗi mode: ~530 LUT lookup -> ~10 us.
- Cả hai mode: ~20 us mỗi frame.

Không đáng kể.

### Tuning Recipe

| Symptom | Adjust |
|---|---|
| Shot hơi trên đầu target gần | Giảm `close_pct` (thử 12-15) |
| Shot vào cổ target gần | Tăng `close_pct` (thử 22-25) |
| Shot miss trên target xa nhỏ | Tăng `mid_min_h` (thử 15) hoặc giảm `mid_pct` (thử 6) |
| Outline jagged, walk-down dừng sớm | Tăng `gap_tolerance` (thử 3-4) |
| Nhiều enemy chồng nhau làm band nhầm | Ép `band_rows = 2` (ít averaging hơn, "head-only" hơn) |

---

## 5.6. Trigger Polygon Check (MỚI 2026-05-31)

`cfg::trigger_polygon_check` (default true) chọn trigger mode:

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

Crosshair "bên trong" vùng tím khi và chỉ khi CẢ BỐN ray chính chạm tím trong trigger FOV. Reject:
- UI tím một phía (HP bar, ability icon, muzzle flash - chỉ 1 hoặc 2 ray hit).
- Crosshair vừa ngoài enemy outline (ray phía xa thoát vào khoảng trống).

Short-circuit khi cả bốn ray hit, nên cost kỳ vọng trên frame "inside enemy" thật
chỉ ~2-4 pixel probe.

### Legacy Mode (`trigger_polygon_check = false`)

Spiral-first-hit: bắn trên BẤT KỲ pixel tím nào trong trigger FOV. Nhanh hơn
nhưng nhiễu hơn (false fire trên UI tím cô lập).

### Trade-off

Polygon check là default vì trong Valorant hầu hết false fire đến từ UI element
một phía (purple skill icon, HP bar, v.v.). Nó giảm nhẹ fire rate trên partial outline
(chỉ 1-3 ray hit qua outline mỏng) nhưng đổi lại precision tốt hơn.

---

## 5.7. Distance-Aware Aimbot Smoothing (MỚI 2026-05-31)

`cfg::apply_delta_dist_smoothing` (default true) scale output từng frame của aimbot
bằng `dist_factor` dựa trên `|delta|`:

| Condition | dist_factor | Default |
|---|---|---|
| `|delta|^2 < apply_delta_near_dist^2` | `apply_delta_near_mult` | 0.4 |
| `|delta|^2 < apply_delta_mid_dist^2` | `apply_delta_mid_mult` | 0.7 |
| else | 1.0 | (không scale) |

### Vì sao dùng Squared Distance

Bỏ sqrt. So sánh với threshold bình phương. Toán tương đương, rẻ hơn ~5 cycle mỗi frame.

### Effect

Close target (`|delta|` < 10): aimbot di chuyển 40% bình thường -> micro-correction mịn hơn, ít overshoot gần target.

Mid target (`|delta|` 10-30): aimbot di chuyển 70% -> tracking mượt hơn khi pursuit.

Far target (`|delta|` >= 30): aimbot full speed -> snap nhanh về target.

Nếu không có cái này, aimbot sẽ oscillate quanh target ("buzz") khi rất gần vì full-speed move mỗi frame overshoot.

### Chỉ ảnh hưởng apply_delta

Magnet (assist) KHÔNG dùng distance-aware scaling. SnapShoot_P/F (silent/flicker) là one-shot - không smoothing.

### Tuning

Nếu aimbot oscillate: giảm `near_mult` (thử 0.25) và `mid_mult` (thử 0.55).
Nếu aimbot lết vào close range: tăng `near_dist` (thử 15) và `near_mult` (thử 0.55).
Nếu aimbot quá chậm ở xa: về lý thuyết không xảy ra vì `dist >= mid_dist -> 1.0`.
Nếu muốn smoothing thuần không theo distance: tắt master toggle.

---

## 6. Color Detection - Chi tiết

### LUT Construction

`std::array<bool, 16777216>` 16MB được index bằng `R * 65536 + G * 256 + B`. Mỗi entry precompute:

```
For every (R, G, B) in [0,255]^3:
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

Build time: ~50ms trên CPU hiện đại. Trigger khi `color_mode` đổi hoặc `useIstrigFilter` toggle.

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

Không dùng floating point. Toàn bộ là integer division.

### Color Mode RGB/HSV Ranges

| Mode | RGB Min | RGB Max | HSV Min | HSV Max |
|------|---------|---------|---------|---------|
| 0 (Purple) | (70, 0, 120) | (255, 190, 255) | (270, 38, 40) | (310, 100, 100) |
| 1 (Anti-Purple) | (70, 110, 120) | (255, 190, 255) | (270, 25, 40) | (310, 100, 100) |
| 2 (Yellow) | (168, 168, 0) | (255, 255, 110) | (55, 5, 70) | (65, 100, 100) |
| 3 (Red) | (225, 45, 45) | (255, 136, 136) | (0, 37, 88) | (1, 80, 100) |

**Anti-Purple vs Purple:** Anti-Purple có green minimum cao hơn (110 vs 0), reject dark purple có thể match UI element hoặc shadow.

**Red hue detection:** HSV range (0, 37, 88)-(1, 80, 100) dùng hue wrap check:
`H <= 30 || H >= 330`. Điều này xử lý đúng red hue nằm quanh biên 0°/360°.

### Spiral Search

Spiral search đi từ tâm vùng capture ra ngoài, từng vòng:

```
for radius = 0 to max_radius:
 scan top edge of ring (y = center - radius)
 scan bottom edge of ring (y = center + radius)
 scan left edge of ring (x = center - radius), excluding corners
 scan right edge of ring (x = center + radius), excluding corners

 if found && radius^2 > bestDist^2:
 break // no closer pixel possible in outer rings
```

**Early exit:** Khi tìm thấy pixel match và khoảng cách tối thiểu có thể của ring hiện tại (`radius^2`) vượt best distance đã tìm, search dừng. Target gần được tìm trong microsecond.

**Worst case:** Không có pixel match - scan toàn vùng FOV. Với FOV 100, đó là
10,000 pixel x 1 LUT lookup mỗi pixel ~= 50-100us.

---

## 7. GPU Compute Path - Chi tiết

### Giới hạn

| Limitation | Value | Reason |
|------------|-------|--------|
| Max FOV mỗi trục | 255 | Pixel coord pack trong 8 bit: `(dist2 << 16) \| (y << 8) \| x` |
| LUT upload | 16MB 3D texture | Format `R8_UNORM`, 256^3 voxel |
| Readback | 4 byte | Single uint32 result buffer |
| Fallback | Tự động | Nếu FOV > 255 hoặc GPU init fail, fallback CPU |

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

`InterlockedMin` đảm bảo closest pixel thắng khi nhiều thread tìm match cùng lúc.

### Khi nào dùng GPU

| Scenario | Recommendation |
|----------|----------------|
| FOV <= 255 + discrete GPU | GPU có thể giúp |
| FOV > 255 | Chỉ CPU (auto-fallback) |
| Integrated GPU | CPU thường nhanh hơn |
| CPU load cao từ app khác | GPU offload detection |
| GPU đang utilization cao | CPU để tránh contention |

---

## 8. Interaction Matrix

Setting nào ảnh hưởng feature nào:

| Setting | Aimbot | Silent | Flicker | Trigger | Assist |
|---------|--------|--------|---------|---------|--------|
| `apply_delta_fov` | filter + MAX-FOV | | | | |
| `mode_a_fov` | | filter + MAX-FOV | | | |
| `nonmode_a_fov` | | | filter + MAX-FOV | | |
| `apply_deltaassist_fov` | | | | | MAX-FOV only |
| `target_offset_x/y` | aim point | | | | (reads aimbot's) |
| `mode_a_target_offset_x/y` | | aim point | aim point | | |
| `color_mode` | LUT | LUT | LUT | (fixed color + sens) | LUT |
| `useIstrigFilter` | LUT | LUT | LUT | không | LUT |
| `distance` | | multiplier | | | |
| `nonmode_a_distance` | | | multiplier | | |
| `apply_delta_smooth` | có | | | | |
| `speed` | có | | | | |
| `apply_delta_dist_smoothing` + tiers | có | | | | |
| `apply_deltaassist_smooth` | | | | | có |
| `assist_speed` | | | | | có |
| `mode_a_head_targeting` | | anchor | anchor | | |
| `head_anchor_proportional` + 6 tiers | | anchor | anchor | | |
| `mode_a_cooldown_ms` | | Arduino | | | |
| `dead_body_filter` + threshold | | suppress | | | |
| `trigger_polygon_check` | | | | algorithm | |
| `min_cluster_size` | per-mode | per-mode | per-mode | | per-mode |

**Insight chính**:

1. **Coord theo mode** (sau 2026-05-31): mỗi mode đọc cặp coord RIÊNG.
 Silent đọc `mode_a_x/y`, flicker đọc `nonmode_a_x/y`,
 aimbot đọc `apply_delta_x/y`. Không sharing, không race.
2. **Mọi biến FOV** đóng góp vào `ComputeMaxFov()` để đặt kích thước vùng capture.
 Coord từng mode cũng được filter bằng half-FOV RIÊNG trong `FindTargets`.
3. **Flicker dùng offset của silent** (`mode_a_target_offset_x/y`) - set bởi
 capture thread khi populate `nonmode_a_x/y`. KHÔNG dùng offset aimbot.
4. **Head targeting + proportional refinement** dùng chung giữa silent + flicker.
5. **Cluster validation theo mode** - detection nhiễu ở một mode không blank mode khác.
6. **Distance-aware smoothing chỉ áp cho aimbot** (`apply_delta`).
 Magnet/SnapShoot_P/F không dùng.
7. **Trigger dùng fixed color reference riêng** + tolerance (không LUT).

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

### Riêng Silent Aim

Thread `mode_a` poll mỗi 1ms. Worst case mất tới 1ms để detect keypress sau khi bấm vật lý.
Cộng thêm vào chuỗi trên.

Debounce 100ms không thêm latency cho lần nhấn đầu - nó chỉ ngăn retrigger trong 100ms sau lần bắn trước.

---

## 10. Calibration Procedures

### Silent Aim Distance - Precision Method

**Setup:** Practice range, static target, aimbot OFF, silent aim ON.

**Step 1 - Coarse:**
```
Set distance = 1.0
Fire at target 10px from center
Observe: shot lands ~4px from target (undershooting)
-> distance needed ~= 10 / (10 - 4) x 1.0 ~= 1.67
```

Nhưng cách đơn giản hơn: `moveX = deltaX x distance`, nên:
```
If you need the shot to travel 10 pixels and it traveled 4:
ratio = 10 / 4 = 2.5
New distance = old_distance x ratio = 1.0 x 2.5 = 2.5
```

**Step 2 - Fine:**
```
Set distance = 2.5
Fire at 10px offset -> lands 1px short
ratio = 10 / 9 = 1.11
New distance = 2.5 x 1.11 = 2.78
```

**Step 3 - Verify:** Test ở offset 3px, 8px, 15px. Tất cả nên hit ngang nhau vì formula tuyến tính.

### FOV - Tìm Sweet Spot

**Cho aimbot:**
1. Bắt đầu FOV 50
2. Chơi bình thường - nếu aimbot không activate đủ thường xuyên, tăng 10
3. Nếu lock sai target hoặc trông thiếu tự nhiên, giảm 10
4. Sweet spot thường là 60-100

**Cho silent aim:**
1. Bắt đầu FOV 60
2. Nếu bị body shot (detection tìm body pixel), giảm xuống 40-50
3. Nếu không bắn khi cần, tăng lên 70-80
4. Nhớ: FOV nhỏ hơn = detection chỉ tìm pixel gần crosshair = dễ ngang đầu hơn
