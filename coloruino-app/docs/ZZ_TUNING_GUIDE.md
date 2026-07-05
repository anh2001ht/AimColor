# Coloruino Tuning Guide

Hướng dẫn dễ hiểu cho mọi setting. Không code, không thuật ngữ rườm rà - chỉ nói từng setting làm gì, chỉnh thế nào, và cần để ý gì.

---

## Trước khi bắt đầu

### Windows Mouse Settings (BẮT BUỘC)
1. Mở **Control Panel -> Mouse -> Pointer Options**
2. Đặt speed slider ở **vị trí giữa** (nấc 6/11)
3. **Bỏ chọn** "Enhance pointer precision"
4. Click Apply

Nếu một trong hai thứ này sai, tuning kiểu gì cũng không nhất quán.

### In-Game Settings
- Mouse acceleration -> **OFF**
- Raw input -> **ON** (nếu game có tùy chọn này)
- Ghi lại **in-game sensitivity** - bạn sẽ cần nó cho setting Distance

---

## Settings theo Feature

---

## 1. Aimbot (apply_delta)

Aimbot liên tục kéo crosshair về target được detect khi bạn giữ activation key. Đây là feature tracking chính.

### Active (on/off)
Bật hoặc tắt hoàn toàn aimbot.

### Key 1 và Key 2
Hai phím activation. Aimbot kích hoạt khi **một trong hai** phím đang được giữ. Đa số đặt một phím là nút hông chuột và một phím là Shift hoặc Ctrl.

Mã phím phổ biến:
| Code | Key |
|------|-----|
| 1 | Left mouse button |
| 2 | Right mouse button |
| 5 | Mouse side button 1 (back) |
| 6 | Mouse side button 2 (forward) |
| 16 | Shift |
| 17 | Ctrl |
| 18 | Alt |

### FOV (1-200)
**Làm gì:** Đặt kích thước vùng aimbot scan target, tính bằng pixel từ tâm màn hình. FOV 82 nghĩa là box 82x82 pixel centered trên crosshair.

**Cách nghĩ:**
- **FOV nhỏ (30-60)** - chỉ lock khi crosshair đã rất gần target. Trông tự nhiên hơn nhưng bạn phải tự làm phần lớn.
- **FOV vừa (60-100)** - cân bằng. Bắt target cách crosshair vừa phải.
- **FOV lớn (100-200)** - snap từ xa. Mạnh hơn, dễ lộ với spectator hơn.

**Cần để ý:**
- FOV lớn có thể lock sai target nếu nhiều địch trên màn hình
- FOV lớn làm movement kém tự nhiên
- FOV ảnh hưởng MỌI feature dùng chung capture - khi nhiều feature bật, hệ thống dùng FOV lớn nhất của bất kỳ feature nào ở thời điểm đó

**Bắt đầu với:** 60-80, tăng nếu bạn miss target acquisition, giảm nếu trông thiếu tự nhiên.

### Smooth (1.0-4.0)
**Làm gì:** Điều khiển crosshair đi về target nhanh thế nào. Raw movement bị chia cho số này.

- **1.0** - nhanh nhất, gần như snap tức thì tới target
- **2.0** - mất khoảng 2 frame để tới target
- **4.0** - chậm, kéo dần về target

**Cách nghĩ:**
- Smooth thấp = mạnh hơn, lộ hơn, tốt cho close-range fight
- Smooth cao = trông tự nhiên hơn, chống spectator tốt hơn, nhưng có thể không track được target strafe nhanh

**Cần để ý:**
- Smooth = 1.0 làm chuyển động crosshair trông máy móc
- Smooth = 4.0 có thể mất track target strafe nhanh
- Smooth làm việc cùng Speed - chúng nhân với nhau

**Bắt đầu với:** 1.2-1.8

### Speed (0.1-1.5)
**Làm gì:** Multiplier trên movement đã smooth. Sau khi raw delta chia cho Smooth, nó được nhân với Speed.

**Cách nghĩ:**
- Speed < 1.0 - crosshair đi chậm hơn tốc độ "true" (subtle hơn)
- Speed = 1.0 - movement khớp đúng phép tính smooth
- Speed > 1.0 - crosshair đi nhanh hơn tính toán (aggressive hơn)

**Quan hệ:** `actual_movement = (pixel_offset / smooth) x speed`

- Smooth 1.4 + Speed 0.4 -> kéo rất nhẹ (tới target trong ~3-4 frame)
- Smooth 1.0 + Speed 1.0 -> lock tức thì
- Smooth 2.0 + Speed 0.8 -> kéo vừa, tự nhiên

**Bắt đầu với:** 0.3-0.6 với smooth khoảng 1.2-1.6

### Offset X (-50 tới 50) và Offset Y (-20 tới 100)
**Làm gì:** Dịch điểm aimbot aim tương đối với pixel top-head được detect.

Color detection tìm outline của địch. Aimbot dùng **pixel tím có Y cao nhất** (đỉnh đầu) làm anchor.

- **Offset X** - dịch aim trái/phải. Dương = phải.
- **Offset Y** - dịch aim xuống (dương). 0 = đỉnh đầu, 5 = trán/mắt, 25 = cổ, 50 = body center.

**Cách nghĩ:**
Anchor nằm ở đỉnh outline đầu. Bạn muốn aim thấp hơn một chút - vào hitbox đầu thực (trán/mắt) hoặc thấp hơn xuống body. Chọn Y offset khớp điểm aim bạn thích.

**Cần để ý:**
- Nếu liên tục bắn trên đầu -> tăng Offset Y
- Nếu liên tục bắn body -> giảm Offset Y
- Offset X thường để 0-2 vì aim ngang thường đã centered
- Offset áp mỗi frame, nên cộng dồn với smooth/speed
- Y âm aim TRÊN đầu (hiếm khi hữu ích)

**Bắt đầu với:** Offset X = 0-1, Offset Y = 3-6

### Sleep (0-100)
**Làm gì:** Thêm delay tính bằng mili giây giữa các frame capture. Làm aimbot phản ứng chậm hơn.

- **0** - nhanh nhất có thể (capture theo refresh rate display)
- **Giá trị cao hơn** - tracking chậm hơn, ít aggressive hơn

**Khi dùng:** Chủ yếu để 0. Chỉ tăng nếu bạn cố tình muốn tracking ì hơn để giống người hơn.

### Distance-Aware Smoothing (MỚI)

Một **card riêng** trong WebUI dưới tab Aimbot. Nó scale movement từng frame của aimbot bằng multiplier phụ thuộc target cách crosshair bao xa:

- **Close** (< Near Distance): dùng Near Multiplier (default 0.4 -> 40% speed)
- **Mid** (< Mid Distance): dùng Mid Multiplier (default 0.7 -> 70% speed)
- **Far** (>= Mid Distance): full speed (1.0)

Vì sao: khi crosshair gần như đã vào target, full-speed movement overshoot và aimbot "buzz" quanh target. Chậm hơn ở close range = micro-correction mịn hơn.

**Settings:**
| Setting | Default | Làm gì |
|---|---|---|
| Enabled | ON | Master toggle cho distance-aware scaling |
| Near distance (px) | 10 | Dưới mức này dùng Near Multiplier |
| Mid distance (px) | 30 | Dưới mức này dùng Mid Multiplier |
| Near multiplier | 0.4 | Speed scale khi rất gần |
| Mid multiplier | 0.7 | Speed scale ở mid range |

**Khi tắt:** Nếu bạn muốn aimbot "snappy" suốt đường tới target (aggressive hơn nhưng jittery gần điểm aim).

**Mẹo tuning:**
- Nếu aimbot oscillate gần target: hạ Near Multiplier (thử 0.25-0.3)
- Nếu aimbot ì trong close fight: tăng Near Distance (thử 15) và Near Multiplier (thử 0.55)
- Nếu muốn threshold sắc hơn: đưa Near và Mid lại gần nhau (vd Near=15, Mid=20)

---

## 2. Silent Aim (mode_a)

Silent aim là flick một phát. Khi nhấn phím, nó lập tức di chuyển tới target, click, rồi snap về vị trí ban đầu - tất cả trong một frame. Nếu tune đúng, bạn không thấy crosshair di chuyển.

### Active (on/off)
Bật/tắt silent aim.

### Key
Phím activation. Khác aimbot, mode này bắn **một lần mỗi lần nhấn** (edge-triggered, không phải hold). Nhấn một lần -> một shot. Nhấn lại -> shot khác.

**Quan trọng:** Key có debounce 100ms. Nhấn nhanh hơn 10 lần/giây sẽ không register shot thêm. Arduino cũng có cooldown 200ms ở phía nó.

### FOV (1-200)
**Làm gì:** Cùng khái niệm với aimbot FOV - scan target bao xa từ crosshair.

**Nhưng với silent aim, nó khác aimbot FOV:**
- Silent aim bắn MỘT LẦN, tức thì. Không tracking.
- FOV quyết định target xa crosshair bao nhiêu thì silent aim vẫn activate
- FOV lớn = bắn target xa crosshair hơn
- **Nhưng accuracy giảm theo khoảng cách** vì movement lớn hơn

**Cần để ý:**
- Nếu FOV quá lớn (100+), bạn có thể bắn target quá lệch tâm, gây jump crosshair thấy được
- Nếu FOV quá nhỏ (30-), bạn cần crosshair rất gần target trước khi nó hoạt động - lúc đó gần như click thường cũng được
- Silent aim dùng FOV **riêng** override capture FOV khi bắn
- Sau khi bắn, capture quay lại FOV của feature đang active khác

**Bắt đầu với:** 60-80. Thấp hơn = mỗi shot chính xác hơn nhưng khó trigger. Cao hơn = dễ trigger nhưng kém chính xác.

### Distance (0.001-10.0)
**Làm gì:** Đây là setting quan trọng nhất cho độ chính xác silent aim. Nó là multiplier chuyển "target cách bao nhiêu pixel" thành "cần di chuột bao nhiêu unit".

**Toán học (đơn giản):** `mouse_movement = pixel_offset x distance`

Giá trị đúng phụ thuộc:
- In-game sensitivity của bạn
- Resolution (bạn đang ở 1920x1080)
- FOV của game

**Nếu distance quá cao:** Bạn overshoot qua target (crosshair đi quá sau lưng họ).
**Nếu distance quá thấp:** Bạn undershoot (crosshair dừng trước target, bắn body hoặc miss).

**Cách calibrate:**
1. Vào practice mode với bot đứng yên
2. Đặt crosshair cách đầu target vài pixel
3. Bắn silent aim
4. Xem shot rơi ở đâu:
 - Overshooting -> giảm distance 0.2
 - Undershooting -> tăng distance 0.2
5. Mỗi lần giảm nửa bước chỉnh: 0.2 -> 0.1 -> 0.05
6. Test nhiều khoảng cách tới target (gần và xa)
7. 5-6 vòng là chỉnh khá chuẩn

**Cần để ý:**
- Distance là cùng một giá trị cho mọi range - nếu đúng ở 3 pixel thì cũng nên đúng ở 15 pixel (formula tuyến tính)
- Nếu đúng ở gần nhưng miss ở xa, vấn đề có lẽ là **pixel nào được detect** (body vs head), không phải distance
- Đổi in-game sensitivity nghĩa là cần recalibrate setting này

**Bắt đầu với:** Thử 2.0-3.0 cho setup low-sens phổ biến (sens 0.3-0.5 @ 800-1600 DPI), rồi chỉnh tiếp.

### Offset X (-100 tới 100) và Offset Y (-100 tới 100)
**Làm gì:** Cùng khái niệm với aimbot offset, nhưng độc lập. Dịch điểm aim tương đối với pixel được detect.

**Với silent aim, phần này rất quan trọng:**
- Pixel detect có thể là đỉnh outline đầu -> offset đưa aim xuống đầu
- Hoặc pixel detect có thể là body/shoulder -> offset không cứu được nếu bạn đang aim vào pixel sai hoàn toàn

**Khác với aimbot offsets:** Silent aim offsets có range rộng hơn (-100 tới 100 so với 0 tới 20) và có thể âm (aim theo mọi hướng từ pixel detect).

**Bắt đầu với:** X = 0, Y = 2-4. Nếu bắn trên đầu, tăng Y. Nếu bắn cổ/body, giảm Y.

### Head Targeting (on/off)
**Làm gì:** Khi ON, silent aim scan lên trên từ pixel detect gần nhất để tìm đỉnh outline địch (đầu). Khi OFF, nó aim vào pixel spiral search tìm thấy đầu tiên (thường là vai hoặc cạnh body).

**Vì sao quan trọng:** Spiral search tìm pixel tím gần crosshair nhất. Nếu bạn đang aim ngang ngực, pixel đó thường là vai hoặc tay trên. Head targeting scan lên từ pixel đó, đi theo outline body khi nó hẹp dần về đầu, rồi aim vào đỉnh.

**Khi tắt:** Nếu shot liên tục trên đầu, hoặc game có effect dọc cao (ability visual, mũ cao) kéo dài trên outline đầu.

**Bắt đầu với:** ON. Đây là cải thiện accuracy lớn nhất cho headshot silent aim.

### Cooldown (50-500 ms)
**Làm gì:** Thời gian tối thiểu giữa các shot silent aim phía Arduino. Dù PC gửi lệnh P liên tục, Arduino không bắn nhanh hơn interval này.

**Vì sao quan trọng:** Ngăn double-fire do keyboard bounce hoặc nhấn quá nhanh. Cũng ngăn silent aim bắn trước khi snapback shot trước hoàn tất.

**Bắt đầu với:** 50ms. Thấp hơn cho follow-up shot nhanh, cao hơn nếu bị accidental double-fire.

---

## 2.5. Head Anchor Refinement (MỚI)

Nằm trong tab RIÊNG của WebUI ("Head Anchor"). Ảnh hưởng silent aim VÀ flicker khi **Head Targeting** bật. Thêm ba cải tiến trên bare topmost-Y anchor:

1. **Walk-down height estimation** - đo enemy cao bao nhiêu bằng cách đi xuống từ head pixel theo outline.
2. **Shoulder-band X averaging** - average X position qua vài row tím trên cùng. Ở xa, nó bắt cả hai vai, midpoint là body centre (ngay dưới đầu).
3. **Proportional Y offset** - địch lớn hơn nhận offset lớn hơn tới trán/mắt; địch xa nhỏ nhận 0 (aim crown).

**Vì sao quan trọng:** Nếu không có phần này, pixel topmost-Y đôi khi là MỘT vai của outline partial -> aim lệch tâm. Proportional Y offset thích nghi theo khoảng cách địch - offset pixel cố định miss cả địch gần cao (offset quá nhỏ) LẪN địch xa nhỏ (offset lớn hơn đầu).

### Enabled (on/off)
Master toggle. Khi OFF, silent/flicker chỉ dùng bare topmost-Y pixel làm anchor (legacy). Khi ON (default), cả ba refinement chạy.

### Band rows (0-20, default 0 = auto)
Bao nhiêu row pixel tím dùng để average shoulder-band X position.
- **0 (auto)** - height/4 clamp 2-6. Tự thích nghi theo kích thước địch. Khuyến nghị.
- **2-3** - ít row hơn = precision "head-only" hơn nhưng dễ nhiễu outline.
- **5-10** - nhiều row hơn = average nhiều hơn, gồm nhiều body hơn. Tốt nếu outline lởm chởm.
- **> 10** - quá nhiều row, sẽ gồm quá nhiều width body. Đừng.

### Outline gap tolerance (0-10, default 2)
Số row non-purple walk-down cho phép trước khi dừng đo height.
- **0** - dừng ở row non-purple đầu. Nghiêm.
- **2 (default)** - xử lý break nhỏ của outline (hair gap, kính/mũ trong suốt).
- **5+** - rất rộng; có thể overshoot xuống body với outline partial.

### Close target offset % (0-50, default 18)
Với địch đủ cao để xem là "close" (>= Close Min Height), Y offset bằng phần trăm này của measured height.
- 18% của địch 50px = 9 px -> trán/mắt.
- Thấp hơn (12-15) nếu shot rơi quá cao.
- Cao hơn (22-25) nếu shot rơi cổ.

### Close target min height (5-200, default 30)
Min enemy height (px) để áp Close offset. Địch cao hơn mức này là "close range."

### Mid target offset % (0-50, default 10)
Với địch mid-range (giữa Mid Min và Close Min height), Y offset = phần trăm này của height.

### Mid target min height (1-100, default 10)
Min enemy height (px) để áp Mid offset. Địch giữa giá trị này và Close Min nhận Mid offset. Dưới mức này -> không offset (aim crown).

**Mẹo tuning:**
- Shot cao trên close target -> giảm Close %
- Shot thấp trên close target -> tăng Close %
- Shot cao trên target xa nhỏ -> giảm Mid % hoặc tăng Mid Min Height
- Nhiều enemy stack làm band nhầm -> đặt Band Rows thủ công = 2
- Outline quá jagged -> tăng Gap Tolerance lên 3-4

---

## 3. Triggerbot (trigger_action)

Triggerbot tự click khi pixel màu target nằm đúng dưới (hoặc rất gần) crosshair. Bạn aim thủ công - nó chỉ xử lý timing click.

### Active (on/off)
Bật/tắt triggerbot.

### Key
Giữ phím này để activate triggerbot. Khi giữ, nếu pixel match vào vùng FOV quanh crosshair, nó click.

### FOV X (1-20) và FOV Y (1-20)
**Làm gì:** Đặt vùng scan quanh crosshair. Đây là box nhỏ: FOV X = 3 nghĩa là rộng 6 pixel (3 mỗi bên), FOV Y = 3 nghĩa là cao 6 pixel.

**Cách nghĩ:**
- **Nhỏ (1-3)** - chỉ bắn khi target gần như dead-center trên crosshair. Rất chính xác, ít false positive.
- **Lớn (5-10)** - bắn khi target gần crosshair. Dễ hơn nhưng có thể bắn khi bạn chưa aim chuẩn.
- **Rất lớn (10-20)** - bắn mỗi khi target ở gần đâu đó. Dễ gây accidental shot.

**Cần để ý:**
- Triggerbot FOV tính bằng pixel như mọi thứ khác
- Quá lớn thì bắn ở góc xấu
- Quá nhỏ thì gần như bạn tự click cũng được
- Triggerbot không bắn nếu bạn đang giữ left click (ngăn double-fire khi bắn tay)
- Nó bắn một lần mỗi lần giữ key - nhả rồi nhấn lại để bắn tiếp

**Bắt đầu với:** 3-5 cho cả X và Y

### Polygon check (on/off, MỚI)
**Làm gì:** Chọn thuật toán trigger.

- **ON (default)** - 4-ray crossing test. Chỉ bắn nếu cả bốn hướng chính (trái, phải, lên, xuống) từ crosshair chạm pixel tím trong trigger FOV. Nghĩa là crosshair phải ở BÊN TRONG outline địch.
- **OFF** - Legacy spiral-first-hit. Bắn nếu có bất kỳ pixel tím nào trong trigger FOV.

**Vì sao ON là default:** UI element (HP bar, ability icon, muzzle flash) chỉ đặt màu tím ở MỘT phía của crosshair. Polygon check loại chúng. Legacy mode sẽ false-fire vào chúng.

**Khi tắt:** Nếu triggerbot không bắn trên partial outline (ví dụ sliver peek chỉ có 1-2 phía outline địch thấy tại crosshair).

**Bắt đầu với:** ON.

---

## 4. Assist (Magnet)

Assist là aimbot phụ với toggle key. Nhấn phím để bật, nhấn lại để tắt. Khi bật, nó kéo nhẹ crosshair về target.

### Active (on/off)
Bật/tắt hoàn toàn assist.

### Key
**Toggle key** (không phải hold). Nhấn một lần -> assist ON. Nhấn lại -> assist OFF. Giữ state tới khi bạn nhấn lại.

### FOV (1-200)
Cùng aimbot FOV - kích thước vùng scan.

**Với assist, giữ nhỏ.** Assist là helper subtle, không phải full aimbot. FOV lớn + assist = lộ.

### Smooth (1.0-4.0)
Cùng aimbot smooth. Cao hơn = kéo nhẹ hơn.

**Với assist, dùng smooth cao hơn** aimbot của bạn. Nếu aimbot là 1.4, đặt assist 2.0-3.0 để subtle hơn.

### Speed (0.1-1.5)
Cùng aimbot speed.

**Với assist, dùng speed thấp hơn** aimbot.

### Offset X và Offset Y
Cùng aimbot offsets.

---

## 5. Flicker (nonmode_a)

Flicker giống silent aim - một lần nhấn. Nó di chuyển tới target và click, nhưng **không snap back**. Crosshair giữ ở vị trí mới.

### Active (on/off)
Bật/tắt flicker.

### Key
Edge-triggered như silent aim. Một lần nhấn = một shot. Cùng debounce 100ms.

### FOV (1-200)
Cùng khái niệm với silent aim FOV.

### Distance (0.1-10.0)
Cùng khái niệm với silent aim distance - multiplier pixel-to-HID-unit. Cần cùng quy trình calibration.

**Flicker distance và silent aim distance là HAI giá trị RIÊNG.** Chúng không dùng chung. Nếu calibrate silent aim, bạn cần calibrate flicker riêng (dù giá trị có thể gần giống).

### Delay (1-50)
Thời gian tối thiểu giữa shot flicker theo mili giây. Ngăn rapid-fire.

---

## 6. Color Settings

### Color Mode (0-3)
Chọn màu cần detect. Phải khớp màu game dùng cho outline địch:

| Mode | Color | Khi dùng |
|------|-------|----------|
| 0 (Purple) | Purple outline | Default cho phần lớn game có outline địch màu tím |
| 1 (Anti-Purple) | Anti-Purple | Dải tím hẹp hơn, giảm false positive |
| 2 (Yellow) | Yellow | Cho game dùng outline vàng |
| 3 (Red) | Red | Cho game dùng outline đỏ |

**Cần để ý:**
- Nếu aimbot cứ lock vào thứ không phải địch (UI element, đồ vật tím khác), thử chuyển từ Purple sang Anti-Purple
- Yellow và Red niche hơn - chỉ dùng nếu game cụ thể dùng màu outline đó

### useIstrigFilter (on/off)
**Làm gì:** Khi ON, mỗi pixel phải pass HAI check - RGB range VÀ HSV range. Khi OFF, chỉ check RGB range.

- **ON** - color matching chính xác hơn, ít false positive hơn, nhưng có thể miss một số target pixel nếu màu của chúng hơi ra ngoài range chặt hơn
- **OFF** - bắt nhiều pixel hơn, nhưng cũng có thể detect thứ không phải target

**Bắt đầu với:** ON. Chỉ tắt nếu aimbot không detect target mà bạn biết nó phải detect.

---

## 7. Performance

### GPU Processing (on/off)
**Làm gì:** Dùng compute shader trên card đồ họa để tìm target pixel thay vì CPU.

**Khi dùng:**
- ON - nếu có discrete GPU và muốn giảm CPU usage một chút
- OFF - nếu GPU processing gây lỗi, hoặc FOV vượt 255 (GPU path giới hạn max 255x255)

**Cần để ý:**
- GPU path có giới hạn FOV 255 pixel mỗi trục. Nếu FOV lớn hơn, nó tự fallback CPU.
- Một số GPU có thể tăng latency nhẹ khi bật GPU mode
- CPU path đủ nhanh cho phần lớn setup

**Bắt đầu với:** OFF. Chỉ bật nếu xác nhận nó hoạt động đúng trên GPU của bạn.

---

## 8. Filtering

### Dead Body Filter (on/off)
**Làm gì:** Khi ON, silent aim bị suppress một frame mỗi khi Y position target detect nhảy quá threshold giữa các frame. Ngăn bắn vào corpse ragdoll.

**Vì sao quan trọng:** Khi bạn giết địch, body rơi xuống. Outline tím còn tồn tại một lúc khi model ragdoll. Aimbot track outline đang rơi này, và nếu silent aim bắn trong lúc ragdoll, nó trúng xác thay vì target kế.

**Cách nghĩ:**
- Target sống bình thường di chuyển vài pixel giữa frame (strafe, đi bộ)
- Ragdoll chết rơi 15-30+ pixel giữa frame
- Filter bắt jump này và skip frame đó

**Khi tắt:** Nếu bạn chơi trong tình huống địch thường xuyên rơi khỏi ledge hoặc teleport (có thể false suppression).

**Bắt đầu với:** ON với threshold 15.

### Dead Body Threshold (3-60 pixels)
**Làm gì:** Kích thước Y-delta jump trigger dead body suppression.

- **Thấp (3-8)** - bắt movement nhỏ hơn. Filtering mạnh hơn, nhưng có thể suppress khi bạn swipe aim dọc nhanh.
- **Vừa (10-20)** - bắt ragdoll drop. Cân bằng tốt.
- **Cao (30-60)** - chỉ bắt drop cực mạnh. Ít false-trigger hơn nhưng có thể miss một số ragdoll.

**Bắt đầu với:** 15.

### Min Cluster Size (0-8)
**Làm gì:** Sau khi spiral search tìm closest purple pixel, nó check 8 pixel xung quanh có bao nhiêu pixel cũng tím. Nếu ít hơn threshold, detection bị reject như noise.

**Vì sao quan trọng:** Pixel tím cô lập có thể xuất hiện từ:
- UI element có tint tím
- Particle effect
- Nhiễu sensor/DXGI

Những pixel lạc này có thể khiến aimbot/silent aim snap tới vị trí ngẫu nhiên.

**Cách nghĩ:**
- **0** - disabled. Mọi pixel detect đều được tin.
- **1-2** - rất dễ dãi. Chỉ reject pixel hoàn toàn cô lập.
- **3-4** - cân bằng. Outline thật có nhiều pixel kề nhau. Single-pixel noise bị lọc.
- **5-8** - aggressive. Có thể reject outline mỏng hoặc xa nơi chỉ thấy vài pixel.

**Bắt đầu với:** 3.

---

## Lỗi thường gặp và cách sửa

### "Silent aim lúc hit head, lúc hit body"
1. **Khả năng cao nhất:** Head Targeting đang OFF. Bật ON - nó scan lên từ closest pixel để tìm đầu thật.
2. **Cũng kiểm tra:** Chỉnh Offset Y. Với head targeting ON, điểm aim bắt đầu ở top đầu. Thử Y = 2-4 để đi vào hitbox đầu.
3. **Nếu head targeting đã ON:** Giảm silent aim FOV xuống 60-70 để detect pixel gần crosshair (có khả năng ngang đầu hơn).
4. **Ít khả năng hơn:** Giá trị Distance sai. Calibrate bằng phương pháp ở trên.

### "Silent aim bắn vào xác / ragdoll"
- Enable **Dead Body Filter** (nên ON mặc định)
- Nếu đã bật, giảm **Dead Body Threshold** (thử 10-12)
- Filter bắt Y jump lớn giữa frame do ragdoll physics

### "Silent aim bắn vào hư không / vị trí ngẫu nhiên"
- Tăng **Min Cluster Size** lên 4-5 để reject single-pixel noise
- Enable **useIstrigFilter** để color matching chặt hơn
- Chuyển sang **Anti-Purple** color mode (mode 1)

### "Aimbot trông robotic / snap quá mạnh"
- Tăng **Smooth** (thử 1.8-2.5)
- Giảm **Speed** (thử 0.2-0.4)
- Giảm **FOV** để chỉ activate khi đã gần target

### "Aimbot lock không đủ nhanh"
- Giảm **Smooth** (thử 1.0-1.3)
- Tăng **Speed** (thử 0.5-0.8)
- Tăng **FOV** để detect target sớm hơn

### "Triggerbot bắn vào hư không / thứ sai"
- Giảm **Trigger FOV X/Y** xuống 2-3
- Chuyển sang **Anti-Purple** color mode (mode 1)
- Enable **useIstrigFilter** cho color matching chặt hơn

### "Aimbot lock sai target khi có nhiều địch"
- Giảm **FOV** - FOV nhỏ hơn nghĩa là nó chỉ lock target gần crosshair nhất
- Spiral search luôn chọn pixel match gần tâm nhất

### "Silent aim overshoot ở long range nhưng close thì ổn"
- Điều này thật ra nghĩa là **Distance** của bạn đúng. Vấn đề là detection đang chọn body pixel ở range xa hơn. Giảm silent aim FOV.

### "Đổi in-game sensitivity, mọi thứ lệch hết"
- Recalibrate **Distance** cho silent aim và flicker
- Aimbot smooth/speed có thể cần tweak nhưng ít nhạy với đổi sens hơn
