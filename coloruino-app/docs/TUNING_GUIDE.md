# Coloruino Tuning Guide

A plain-language guide to every setting. No code, no jargon - just what each setting does, how to adjust it, and what to watch out for.

---

## Before You Start

### Windows Mouse Settings (REQUIRED)
1. Open **Control Panel -> Mouse -> Pointer Options**
2. Set speed slider to the **middle position** (notch 6 of 11)
3. **Uncheck** "Enhance pointer precision"
4. Click Apply

If either of these is wrong, no amount of tuning will make things consistent.

### In-Game Settings
- Mouse acceleration -> **OFF**
- Raw input -> **ON** (if your game has this option)
- Note your **in-game sensitivity** - you'll need it for the Distance setting

---

## Settings by Feature

---

## 1. Aimbot (apply_delta)

The aimbot continuously moves your crosshair toward the detected target while you hold the activation key. It's the main tracking feature.

### Active (on/off)
Turns the aimbot on or off entirely.

### Key 1 and Key 2
Two activation keys. The aimbot activates when **either** key is held down. Most people set one to a mouse side button and one to Shift or Ctrl.

Common key codes:
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
**What it does:** Sets the size of the area the aimbot scans for targets, measured in pixels from the center of your screen. FOV 82 means an 82×82 pixel box centered on your crosshair.

**How to think about it:**
- **Small FOV (30-60)** - only locks on when your crosshair is already very close to the target. Looks more natural but you have to do most of the work yourself.
- **Medium FOV (60-100)** - balanced. Catches targets at moderate distance from your crosshair.
- **Large FOV (100-200)** - snaps from far away. More aggressive, more obvious to spectators.

**What to watch out for:**
- Larger FOV can lock onto the wrong target if multiple enemies are on screen
- Larger FOV makes movement look less natural
- FOV affects ALL features that share the capture - when multiple features are on, the system uses whichever feature's FOV is largest at any given moment

**Start with:** 60-80 and adjust up if you're missing target acquisition, down if it looks unnatural.

### Smooth (1.0-4.0)
**What it does:** Controls how quickly the crosshair moves toward the target. The raw movement is divided by this number.

- **1.0** - fastest, nearly instant snap to target
- **2.0** - takes about 2 frames to reach target
- **4.0** - slow, gradual pull toward target

**How to think about it:**
- Lower smooth = more aggressive, more obvious, better for close-range fights
- Higher smooth = more natural-looking, better for spectator resistance, but may not track fast-moving targets

**What to watch out for:**
- At smooth = 1.0 your crosshair movement looks robotic
- At smooth = 4.0 you might lose track of fast-strafing targets
- Smooth works together with Speed - they multiply

**Start with:** 1.2-1.8

### Speed (0.1-1.5)
**What it does:** A multiplier on top of the smoothed movement. After the raw delta is divided by Smooth, it's multiplied by Speed.

**How to think about it:**
- Speed < 1.0 - crosshair moves slower than "true" speed (more subtle)
- Speed = 1.0 - movement matches the smooth calculation exactly
- Speed > 1.0 - crosshair moves faster than calculated (more aggressive)

**The relationship:** `actual_movement = (pixel_offset / smooth) × speed`

- Smooth 1.4 + Speed 0.4 -> very gentle pull (reaches target in ~3-4 frames)
- Smooth 1.0 + Speed 1.0 -> instant lock
- Smooth 2.0 + Speed 0.8 -> moderate, natural pull

**Start with:** 0.3-0.6 with smooth around 1.2-1.6

### Offset X (-50 to 50) and Offset Y (-20 to 100)
**What it does:** Shifts where the aimbot aims relative to the detected head-top pixel.

The color detection finds the outline of the enemy. The aimbot uses the **topmost-Y purple pixel** (head crown) as the anchor.

- **Offset X** - shifts aim left/right. Positive = right.
- **Offset Y** - shifts aim down (positive). 0 = head crown, 5 = forehead/eyes, 25 = neck, 50 = center mass.

**How to think about it:**
The anchor sits at the head outline crown. You want to aim a bit below that - into the actual head hitbox (forehead/eyes) or further down to body. Pick the Y offset that matches your preferred aim point.

**What to watch out for:**
- If you're consistently hitting above the head -> increase Offset Y
- If you're consistently hitting body -> decrease Offset Y
- Offset X usually stays at 0-2 since horizontal aim is typically centered
- These offsets apply every frame, so they compound with smooth/speed
- Negative Y aims ABOVE the head (rarely useful)

**Start with:** Offset X = 0-1, Offset Y = 3-6

### Sleep (0-100)
**What it does:** Adds a delay in milliseconds between capture frames. Slows down the aimbot's reaction speed.

- **0** - fastest possible (captures at display refresh rate)
- **Higher values** - slower, less aggressive tracking

**When to use:** Mostly leave at 0. Only increase if you want intentionally sluggish tracking for a more human look.

### Distance-Aware Smoothing (NEW)

A whole **separate card** in the WebUI under the Aimbot tab. It scales the aimbot's per-frame movement by an extra multiplier whose magnitude depends on how far the target is from your crosshair:

- **Close** (< Near Distance): use Near Multiplier (default 0.4 -> 40% speed)
- **Mid** (< Mid Distance): use Mid Multiplier (default 0.7 -> 70% speed)
- **Far** (≥ Mid Distance): full speed (1.0)

Why: when the crosshair is already nearly on target, full-speed movement overshoots and the aimbot "buzzes" around the target. Slower at close range = finer micro-correction.

**Settings:**
| Setting | Default | What it does |
|---|---|---|
| Enabled | ON | Master toggle for distance-aware scaling |
| Near distance (px) | 10 | Below this, use Near Multiplier |
| Mid distance (px) | 30 | Below this, use Mid Multiplier |
| Near multiplier | 0.4 | Speed scale when very close |
| Mid multiplier | 0.7 | Speed scale at mid range |

**When to disable:** If you want aimbot to feel "snappy" all the way to the target (more aggressive but jittery near aim point).

**Tuning tips:**
- If aimbot oscillates near target: lower Near Multiplier (try 0.25-0.3)
- If aimbot feels sluggish in close fights: raise Near Distance (try 15) and Near Multiplier (try 0.55)
- If you want a sharper threshold: bring Near and Mid closer together (e.g. Near=15, Mid=20)

---

## 2. Silent Aim (mode_a)

Silent aim is a one-shot flick. When you press the key, it instantly moves to the target, clicks, then snaps back to your original position - all within a single frame. If tuned correctly, you don't see the crosshair move at all.

### Active (on/off)
Turns silent aim on or off.

### Key
The activation key. Unlike aimbot, this fires **once per press** (edge-triggered, not hold). Press once -> one shot. Press again -> another shot.

**Important:** The key has a 100ms debounce. Pressing faster than 10 times per second won't register extra shots. The Arduino also has a 200ms cooldown on its end.

### FOV (1-200)
**What it does:** Same concept as aimbot FOV - how far from your crosshair it scans for targets.

**But for silent aim, this is different than aimbot FOV:**
- Silent aim fires ONCE, instantly. There's no tracking.
- FOV determines how far away a target can be for silent aim to activate
- Larger FOV = fires at targets further from your crosshair
- **But accuracy drops with distance** because the movement gets larger

**What to watch out for:**
- If FOV is too large (100+), you might fire at targets that are too far off-center, causing visible crosshair jumps
- If FOV is too small (30-), you need your crosshair very close to the target before it works - at that point you might as well just click normally
- Silent aim uses its **own** FOV that overrides the capture FOV when it fires
- After firing, the capture switches back to whichever other feature's FOV is active

**Start with:** 60-80. Lower = more accurate per shot but harder to trigger. Higher = easier to trigger but less precise.

### Distance (0.001-10.0)
**What it does:** This is the most important setting for silent aim accuracy. It's a multiplier that converts "how many pixels away the target is" into "how many mouse units to move."

**The math (simplified):** `mouse_movement = pixel_offset × distance`

The correct value depends on:
- Your in-game sensitivity
- Your resolution (you're on 1920×1080)
- Your game's FOV

**If distance is too high:** You overshoot past the target (crosshair goes behind them).
**If distance is too low:** You undershoot (crosshair stops before reaching the target, hitting body or missing).

**How to calibrate:**
1. Go into a practice mode with a standing still bot
2. Position your crosshair a few pixels from the target's head
3. Fire silent aim
4. Watch where the shot lands:
 - Overshooting -> lower the distance by 0.2
 - Undershooting -> raise the distance by 0.2
5. Cut your adjustment in half each time: 0.2 -> 0.1 -> 0.05
6. Test at multiple distances from the target (close and far)
7. 5-6 rounds gets you dialed in

**What to watch out for:**
- Distance is the same value for all ranges - if it's right at 3 pixels, it should be right at 15 pixels too (the formula is linear)
- If it's accurate at close range but misses at far range, the issue is probably **which pixel was detected** (body vs head), not the distance value
- Changing your in-game sensitivity means you need to recalibrate this

**Start with:** Try 2.0-3.0 for typical low-sens setups (sens 0.3-0.5 @ 800-1600 DPI), adjust from there.

### Offset X (-100 to 100) and Offset Y (-100 to 100)
**What it does:** Same concept as aimbot offset, but independent values. Shifts the aim point relative to the detected pixel.

**For silent aim, this matters a lot:**
- The detected pixel might be the top of the head outline -> offset moves your aim down into the head
- Or the detected pixel might be a body/shoulder pixel -> offset can't save you if you're aiming at the wrong pixel entirely

**Difference from aimbot offsets:** Silent aim offsets have a wider range (-100 to 100 vs 0 to 20) and can go negative (aim in any direction from detected pixel).

**Start with:** X = 0, Y = 2-4. If you're hitting above the head, increase Y. If hitting neck/body, decrease Y.

### Head Targeting (on/off)
**What it does:** When ON, silent aim scans upward from the closest detected pixel to find the top of the enemy outline (the head). When OFF, it aims at whatever pixel the spiral search found first (often a shoulder or body edge).

**Why it matters:** The spiral search finds the closest purple pixel to your crosshair. If you're aiming at chest level, that pixel is often a shoulder or upper arm. Head targeting scans upward from that pixel, following the body outline as it narrows toward the head, and aims at the top instead.

**When to turn off:** If you're getting shots above the head consistently, or if the game has tall vertical effects (ability visuals, tall hats) that extend above the head outline.

**Start with:** ON. This is the biggest accuracy improvement for silent aim headshots.

### Cooldown (50-500 ms)
**What it does:** Minimum time between silent aim shots on the Arduino side. Even if the PC sends rapid P commands, the Arduino won't fire faster than this interval.

**Why it matters:** Prevents accidental double-fires from keyboard bounce or rapid pressing. Also prevents the silent aim from firing before the previous shot's snapback has completed.

**Start with:** 50ms. Lower for faster follow-up shots, higher if you're getting accidental double-fires.

---

## 2.5. Head Anchor Refinement (NEW)

Lives in its OWN tab in the WebUI ("Head Anchor"). Affects silent aim AND flicker when **Head Targeting** is on. Adds three refinements on top of the bare topmost-Y anchor:

1. **Walk-down height estimation** - measures how tall the enemy is by walking down from the head pixel along the outline.
2. **Shoulder-band X averaging** - averages the X position across the top few rows of purple pixels. At range, this catches both shoulders, and their midpoint is the body centre (directly under the head).
3. **Proportional Y offset** - bigger enemies get a bigger offset toward forehead/eyes; tiny far enemies get 0 (aim crown).

**Why it matters:** Without this, the topmost-Y pixel is sometimes ONE shoulder of a partial outline -> aim drifts off-centre. The proportional Y offset adapts to enemy distance - a fixed pixel offset misses both tall close enemies (offset too small) AND tiny far ones (offset bigger than the head).

### Enabled (on/off)
Master toggle. When OFF, silent/flicker just use the bare topmost-Y pixel as the anchor (legacy behaviour). When ON (default), all three refinements run.

### Band rows (0-20, default 0 = auto)
How many rows of purple pixels to average for the shoulder-band X position.
- **0 (auto)** - height/4 clamped to 2-6. Adapts to enemy size automatically. Recommended.
- **2-3** - fewer rows = more "head-only" precision but susceptible to outline noise.
- **5-10** - more rows = more averaging, includes more of the body. Good if outline is jagged.
- **> 10** - too many rows, will include too much body width. Don't.

### Outline gap tolerance (0-10, default 2)
How many non-purple rows the walk-down allows before stopping the height measurement.
- **0** - stops at first non-purple row. Strict.
- **2 (default)** - handles small outline breaks (hair gaps, transparent helmet visor).
- **5+** - very lenient; may overshoot down into body for partial outlines.

### Close target offset % (0-50, default 18)
For enemies tall enough to be considered "close" (≥ Close Min Height), the Y offset is this percent of the measured height.
- 18% of a 50px enemy = 9 px -> forehead/eyes.
- Lower (12-15) if shots land too high.
- Higher (22-25) if shots land in necks.

### Close target min height (5-200, default 30)
Min enemy height (px) to apply the Close offset. Enemies taller than this are "close range."

### Mid target offset % (0-50, default 10)
For mid-range enemies (between Mid Min and Close Min height), Y offset = this percent of height.

### Mid target min height (1-100, default 10)
Min enemy height (px) to apply the Mid offset. Enemies between this and Close Min get the Mid offset. Below this -> no offset (aim crown).

**Tuning tips:**
- Shots high on close targets -> lower Close %
- Shots low on close targets -> raise Close %
- Shots high above tiny far targets -> lower Mid % or raise Mid Min Height
- Multi-stack enemies confuse the band -> set Band Rows manually to 2
- Outline very jagged -> raise Gap Tolerance to 3-4

---

## 3. Triggerbot (trigger_action)

The triggerbot automatically clicks when a target-colored pixel is directly under (or very near) your crosshair. You aim manually - it just handles the click timing.

### Active (on/off)
Turns triggerbot on or off.

### Key
Hold this key to activate the triggerbot. While held, if a matching pixel enters the FOV area around your crosshair, it clicks.

### FOV X (1-20) and FOV Y (1-20)
**What it does:** Sets the scan area around your crosshair. This is a small box: FOV X = 3 means 6 pixels wide (3 on each side), FOV Y = 3 means 6 pixels tall.

**How to think about it:**
- **Small (1-3)** - only fires when the target is nearly dead-center on your crosshair. Very precise, fewer false positives.
- **Large (5-10)** - fires when the target is near your crosshair. More forgiving but might fire when you're not perfectly aimed.
- **Very large (10-20)** - fires whenever a target is anywhere close. Will cause accidental shots.

**What to watch out for:**
- Triggerbot FOV is measured in pixels, same as everything else
- Too large and it fires at bad angles
- Too small and you might as well just click yourself
- The triggerbot won't fire if you're already holding left click (prevents double-firing during manual shooting)
- It fires once per key hold - press and release, then press again for another shot

**Start with:** 3-5 for both X and Y

### Polygon check (on/off, NEW)
**What it does:** Selects the trigger algorithm.

- **ON (default)** - 4-ray crossing test. Fires only if all four cardinal directions (left, right, up, down) from the crosshair hit a purple pixel within the trigger FOV. This means the crosshair must be INSIDE an enemy outline.
- **OFF** - Legacy spiral-first-hit. Fires on any purple pixel within the trigger FOV.

**Why ON is the default:** UI elements (HP bars, ability icons, muzzle flash) put purple on only ONE side of the crosshair. Polygon check rejects those. Legacy mode would false-fire on them.

**When to turn OFF:** If the triggerbot doesn't fire on partial outlines (e.g. sliver peeks where only 1-2 sides of the enemy outline are visible at the crosshair).

**Start with:** ON.

---

## 4. Assist (Magnet)

Assist is a secondary aimbot with a toggle key. Press the key to turn it on, press again to turn it off. While on, it gently pulls your crosshair toward the target.

### Active (on/off)
Turns assist on or off entirely.

### Key
**Toggle key** (not hold). Press once -> assist turns ON. Press again -> assist turns OFF. Stays in its current state until you press again.

### FOV (1-200)
Same as aimbot FOV - scan area size.

**For assist, keep this small.** Assist is meant to be a subtle helper, not a full aimbot. Large FOV + assist = obvious.

### Smooth (1.0-4.0)
Same as aimbot smooth. Higher = gentler pull.

**For assist, use higher smooth** than your aimbot. If your aimbot is 1.4, set assist to 2.0-3.0 for a more subtle effect.

### Speed (0.1-1.5)
Same as aimbot speed.

**For assist, use lower speed** than your aimbot.

### Offset X and Offset Y
Same as aimbot offsets.

---

## 5. Flicker (nonmode_a)

Flicker is similar to silent aim - a one-shot press. It moves to the target and clicks, but **doesn't snap back**. Your crosshair stays at the new position.

### Active (on/off)
Turns flicker on or off.

### Key
Edge-triggered like silent aim. One press = one shot. Same 100ms debounce.

### FOV (1-200)
Same concept as silent aim FOV.

### Distance (0.1-10.0)
Same concept as silent aim distance - pixel-to-HID-unit multiplier. Needs the same calibration process.

**Flicker distance and silent aim distance are SEPARATE values.** They don't share. If you calibrate silent aim, you need to calibrate flicker separately (though the values will likely be similar).

### Delay (1-50)
Minimum time between flicker shots in milliseconds. Prevents rapid-fire.

---

## 6. Color Settings

### Color Mode (0-3)
Selects which color to detect. Must match what the game shows for enemy outlines:

| Mode | Color | When to Use |
|------|-------|-------------|
| 0 (Purple) | Purple outline | Default for most games with purple enemy outlines |
| 1 (Anti-Purple) | Anti-Purple | Narrower purple range, reduces false positives |
| 2 (Yellow) | Yellow | For games with yellow outlines |
| 3 (Red) | Red | For games with red outlines |

**What to watch out for:**
- If the aimbot keeps locking onto things that aren't enemies (UI elements, other purple things), try switching from Purple to Anti-Purple
- Yellow and Red modes are more niche - only use if your game specifically uses those outline colors

### useIstrigFilter (on/off)
**What it does:** When ON, each pixel must pass TWO checks - RGB range AND HSV range. When OFF, only RGB range is checked.

- **ON** - more precise color matching, fewer false positives, but might miss some target pixels if their color is slightly outside the tighter range
- **OFF** - catches more pixels, but might also detect things that aren't targets

**Start with:** ON. Only turn off if the aimbot isn't detecting targets you know it should.

---

## 7. Performance

### GPU Processing (on/off)
**What it does:** Uses your graphics card's compute shader to find the target pixel instead of the CPU.

**When to use:**
- ON - if you have a discrete GPU and want slightly lower CPU usage
- OFF - if GPU processing causes issues, or if your FOV exceeds 255 (GPU path is limited to 255×255 maximum)

**What to watch out for:**
- GPU path has a 255 pixel FOV limit per axis. If your FOV is larger, it automatically falls back to CPU anyway.
- On some GPUs, GPU mode can cause slight latency increase
- CPU path is plenty fast for most setups

**Start with:** OFF. Only enable if you've confirmed it works correctly on your GPU.

---

## 8. Filtering

### Dead Body Filter (on/off)
**What it does:** When ON, silent aim is suppressed for one frame whenever the detected target's Y position jumps more than the threshold between frames. This prevents firing at ragdolled corpses.

**Why it matters:** When you kill an enemy, their body drops. The purple outline stays for a moment while the model ragdolls. The aimbot tracks this falling outline, and if silent aim fires during the ragdoll, it hits the corpse instead of the next target.

**How to think about it:**
- A normal living target moves a few pixels between frames (strafing, walking)
- A dying ragdoll drops 15-30+ pixels between frames
- The filter catches this jump and skips that frame

**When to turn off:** If you play in situations where enemies frequently drop off ledges or teleport (would trigger false suppression).

**Start with:** ON with threshold 15.

### Dead Body Threshold (3-60 pixels)
**What it does:** The Y-delta jump size that triggers dead body suppression.

- **Low (3-8)** - catches smaller movements. More aggressive filtering, but might suppress during fast vertical aim swipes.
- **Medium (10-20)** - catches ragdoll drops. Good balance.
- **High (30-60)** - only catches extreme drops. Less likely to false-trigger but might miss some ragdolls.

**Start with:** 15.

### Min Cluster Size (0-8)
**What it does:** After the spiral search finds the closest purple pixel, it checks how many of the 8 surrounding pixels are also purple. If fewer than this threshold, the detection is rejected as noise.

**Why it matters:** Single isolated purple pixels can appear from:
- UI elements with purple tints
- Particle effects
- Sensor/DXGI noise

These stray pixels would cause the aimbot/silent aim to snap to random positions.

**How to think about it:**
- **0** - disabled. Every detected pixel is trusted.
- **1-2** - very permissive. Only rejects completely isolated pixels.
- **3-4** - balanced. A real outline has many adjacent pixels. Single-pixel noise is filtered.
- **5-8** - aggressive. Might reject thin or distant outlines where only a few pixels are visible.

**Start with:** 3.

---

## Common Problems and Fixes

### "Silent aim sometimes hits head, sometimes body"
1. **Most likely:** Head Targeting is OFF. Turn it ON - it scans upward from the closest pixel to find the actual head.
2. **Also check:** Adjust Offset Y. With head targeting ON, the aim point starts at the head top. Try Y = 2-4 to move into the head hitbox.
3. **If head targeting is already ON:** Reduce silent aim FOV to 60-70 so it detects nearby pixels (more likely to be head-level).
4. **Less likely:** Distance value is off. Calibrate using the method described above.

### "Silent aim fires at dead bodies / ragdolls"
- Enable **Dead Body Filter** (should be ON by default)
- If already on, lower the **Dead Body Threshold** (try 10-12)
- The filter catches large Y jumps between frames caused by ragdoll physics

### "Silent aim fires at nothing / random spots"
- Increase **Min Cluster Size** to 4-5 to reject single-pixel noise
- Enable **useIstrigFilter** for tighter color matching
- Switch to **Anti-Purple** color mode (mode 1)

### "Aimbot looks robotic / snaps too hard"
- Increase **Smooth** (try 1.8-2.5)
- Decrease **Speed** (try 0.2-0.4)
- Reduce **FOV** so it only activates when already close

### "Aimbot doesn't lock on fast enough"
- Decrease **Smooth** (try 1.0-1.3)
- Increase **Speed** (try 0.5-0.8)
- Increase **FOV** so it detects targets sooner

### "Triggerbot fires at nothing / wrong things"
- Reduce **Trigger FOV X/Y** to 2-3
- Switch to **Anti-Purple** color mode (mode 1)
- Enable **useIstrigFilter** for tighter color matching

### "Aimbot locks onto wrong target with multiple enemies"
- Reduce **FOV** - smaller FOV means it only locks onto the closest target to your crosshair
- The spiral search always picks the closest matching pixel to center

### "Silent aim overshoots at long range but is fine close"
- This actually means your **Distance** is correct. The issue is the detection picking body pixels at longer range. Reduce silent aim FOV.

### "Changed in-game sensitivity, everything is off now"
- Recalibrate **Distance** for silent aim and flicker
- Aimbot smooth/speed might need tweaking but are less sensitive to sens changes
