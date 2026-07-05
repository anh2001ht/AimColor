#include "CaptureLoop.h"
#include "ScreenCapture.h"
#include "ColorDetector.h"
#include "core/Config.h"
#include "core/Globals.h"
#include "input/MouseMove.h"
#include "network/UDPClient.h"
#include "util/Vec2.h"
#include "util/MathHelpers.h"
#include "util/SystemUtils.h"

#include <thread>
#include <chrono>
#include <mutex>
#include <climits>

// ═══════════════════════════════════════════════════════════════════
// Per-mode candidate state. Each consuming mode (aimbot / silent /
// flicker) has its own candidate tracked over a single linear scan of
// the capture buffer. The buffer is ALWAYS captured at MAX-FOV =
// max of all active modes' FOVs (see ComputeMaxFov), so each mode just
// has to mask off pixels outside its own FOV during the scan.
// ═══════════════════════════════════════════════════════════════════
struct ModeCandidate {
 int bestDist2 = INT_MAX; // L2 distance² from screen centre
 int bestCDx = 0, bestCDy = 0; // closest-to-centre delta
 int bestTopY = INT_MAX; // smallest absolute y (visually highest)
 int bestTDx = 0, bestTDy = 0; // topmost-Y delta
 bool found = false;
};

// ═══════════════════════════════════════════════════════════════════
// Single linear pass - tracks closest-to-centre AND topmost-Y
// candidates SEPARATELY for each active mode, within each mode's
// own FOV box. One scan = one cache pass over the buffer.
//
// topDx/Dy is the proven amdow / UCColor head-top anchor: the
// topmost purple pixel of any visible enemy outline IS the head-top
// regardless of broken contours, hats, hair, partial occlusion, or
// multiple stacked enemies.
//
// closestDx/Dy is used by silent when cfg::mode_a_head_targeting
// is OFF (legacy behaviour).
//
// Per-mode FOV check is just |dx| ≤ halfFov && |dy| ≤ halfFov - a
// square mask matching the capture region geometry. An inactive
// mode is passed halfFov = 0 so its check fails immediately (its
// candidate stays found=false, coords stay 0).
//
// For a 200×200 MAX-FOV buffer this is 40 k LUT lookups (L1 hot) +
// up to 3× cheap branches per matching pixel. Sub-millisecond cost
// at any realistic FOV / refresh rate.
//
// Output deltas are screen-centre-relative (positive = right/down).
// ═══════════════════════════════════════════════════════════════════
static void FindTargets(const BYTE* __restrict data,
 int width, int height,
 int aimbotHalf, ModeCandidate& aimbot,
 int silentHalf, ModeCandidate& silent,
 int flickerHalf, ModeCandidate& flicker)
{
 FastColorDetector::EnsureTable();

 const int stride = width * 4;
 const int halfW = width >> 1;
 const int halfH = height >> 1;

 for (int y = 0; y < height; ++y) {
 const BYTE* row = data + y * stride;
 const int dy = y - halfH;
 const int ady = dy < 0 ? -dy : dy;
 const int dy2 = dy * dy;

 // Skip whole rows that fall outside ALL active modes' FOV bands.
 // (Each mode's box is square; if |dy| exceeds the largest halfFov,
 // no pixel in this row contributes to any candidate.)
 int maxHalf = aimbotHalf;
 if (silentHalf > maxHalf) maxHalf = silentHalf;
 if (flickerHalf > maxHalf) maxHalf = flickerHalf;
 if (ady > maxHalf) continue;

 for (int x = 0; x < width; ++x) {
 const BYTE* px = row + x * 4;
 if (!FastColorDetector::IsTargetColor(px[2], px[1], px[0])) continue;

 const int dx = x - halfW;
 const int adx = dx < 0 ? -dx : dx;
 const int dist2 = dx * dx + dy2;

 // ── Aimbot candidate ─────────────────────────────────────
 if (aimbotHalf > 0 && adx <= aimbotHalf && ady <= aimbotHalf) {
 if (dist2 < aimbot.bestDist2) {
 aimbot.bestDist2 = dist2;
 aimbot.bestCDx = dx; aimbot.bestCDy = dy;
 }
 if (y < aimbot.bestTopY) {
 aimbot.bestTopY = y;
 aimbot.bestTDx = dx; aimbot.bestTDy = dy;
 }
 aimbot.found = true;
 }

 // ── Silent candidate ─────────────────────────────────────
 if (silentHalf > 0 && adx <= silentHalf && ady <= silentHalf) {
 if (dist2 < silent.bestDist2) {
 silent.bestDist2 = dist2;
 silent.bestCDx = dx; silent.bestCDy = dy;
 }
 if (y < silent.bestTopY) {
 silent.bestTopY = y;
 silent.bestTDx = dx; silent.bestTDy = dy;
 }
 silent.found = true;
 }

 // ── Flicker candidate ────────────────────────────────────
 if (flickerHalf > 0 && adx <= flickerHalf && ady <= flickerHalf) {
 if (dist2 < flicker.bestDist2) {
 flicker.bestDist2 = dist2;
 flicker.bestCDx = dx; flicker.bestCDy = dy;
 }
 if (y < flicker.bestTopY) {
 flicker.bestTopY = y;
 flicker.bestTDx = dx; flicker.bestTDy = dy;
 }
 flicker.found = true;
 }
 }
 }
}

// ═══════════════════════════════════════════════════════════════════
// Cluster validation: check that the closest pixel has enough purple
// neighbours to be a real target (not single-pixel noise / UI glitch).
// Returns count of purple pixels among the 8 neighbours.
// ═══════════════════════════════════════════════════════════════════
static int CountNeighbours(const BYTE* __restrict data,
 int width, int height, int dx, int dy)
{
 const int stride = width * 4;
 const int halfW = width >> 1;
 const int halfH = height >> 1;
 const int col = halfW + dx;
 const int row = halfH + dy;
 int count = 0;

 for (int dr = -1; dr <= 1; ++dr) {
 for (int dc = -1; dc <= 1; ++dc) {
 if (dr == 0 && dc == 0) continue;
 int r = row + dr, c = col + dc;
 if (r < 0 || r >= height || c < 0 || c >= width) continue;
 const BYTE* px = data + r * stride + c * 4;
 if (FastColorDetector::IsTargetColor(px[2], px[1], px[0]))
 ++count;
 }
 }
 return count;
}

// ═══════════════════════════════════════════════════════════════════
// Head anchor refinement (Tier-1 port from tfirm reference).
//
// Given a mode candidate that has a topmost-Y pixel, refine the aim
// anchor so it sits on head-centre (not one shoulder) and gets a
// per-shot Y offset that scales with measured enemy height.
//
// Step 1 - walk down from the topmost pixel's column to estimate
// enemy height in pixels. Allows up to
// cfg::head_anchor_gap_tolerance consecutive non-purple
// rows (handles broken outline / hair gaps).
//
// Step 2 - average X across the top N rows of purple pixels inside
// the mode's FOV. N = cfg::head_anchor_band_rows
// (0 ⇒ auto: clamp(height/4, 2, 6)). On far targets the
// band catches both shoulders; their midpoint is body
// centre (directly under the head). On near targets the
// band is just the head crown - also the right answer.
//
// Step 3 - proportional Y offset. Bigger enemies get a bigger
// offset toward forehead/eyes; tiny enemies get 0 (aim
// crown directly so we don't over-shoot above the head).
//
// Outputs are delta-from-buffer-centre, matching the rest of the
// FindTargets coordinate convention. Returns false if the candidate
// itself wasn't found (caller should leave coords at 0).
// ═══════════════════════════════════════════════════════════════════
static bool RefineHeadAnchor(const BYTE* __restrict data,
 int bufW, int bufH, int stride,
 const ModeCandidate& cand,
 int modeHalfFov,
 int& outDx, int& outDy)
{
 if (!cand.found) return false;
 if (!cfg::head_anchor_proportional) {
 // Caller will use the topmost-Y anchor directly.
 outDx = cand.bestTDx;
 outDy = cand.bestTDy;
 return true;
 }

 const int halfW = bufW >> 1;
 const int halfH = bufH >> 1;

 // Convert topmost delta back into buffer coordinates.
 const int topYBuf = cand.bestTopY; // already buffer y
 const int topXBuf = cand.bestTDx + halfW; // delta → buffer
 if (topXBuf < 0 || topXBuf >= bufW) {
 outDx = cand.bestTDx;
 outDy = cand.bestTDy;
 return true;
 }

 // ── Step 1: walk down the topmost column to estimate height ──
 const int gapAllowed = cfg::head_anchor_gap_tolerance < 0
 ? 0 : cfg::head_anchor_gap_tolerance;
 int botYBuf = topYBuf;
 int gap = 0;
 for (int y = topYBuf + 1; y < bufH; ++y) {
 const BYTE* px = data + y * stride + topXBuf * 4;
 if (FastColorDetector::IsTargetColor(px[2], px[1], px[0])) {
 botYBuf = y;
 gap = 0;
 } else {
 if (++gap > gapAllowed) break;
 }
 }
 int targetH = botYBuf - topYBuf + 1;
 if (targetH < 1) targetH = 1;

 // ── Step 2: shoulder-band X averaging ────────────────────────
 int bandRows = cfg::head_anchor_band_rows;
 if (bandRows <= 0) {
 bandRows = targetH / 4;
 if (bandRows < 2) bandRows = 2;
 if (bandRows > 6) bandRows = 6;
 }

 // Constrain band scan to mode's FOV horizontally so unrelated
 // purples outside the mode's box don't bias the average.
 const int xLo = (modeHalfFov > 0)
 ? FastMax(0, halfW - modeHalfFov)
 : 0;
 const int xHi = (modeHalfFov > 0)
 ? FastMin(bufW - 1, halfW + modeHalfFov)
 : (bufW - 1);
 const int yLo = topYBuf;
 const int yHi = FastMin(bufH, topYBuf + bandRows);

 long long sumX = 0;
 int nPx = 0;
 for (int y = yLo; y < yHi; ++y) {
 const BYTE* row = data + y * stride;
 for (int x = xLo; x <= xHi; ++x) {
 const BYTE* px = row + x * 4;
 if (FastColorDetector::IsTargetColor(px[2], px[1], px[0])) {
 sumX += x;
 ++nPx;
 }
 }
 }
 int anchorXBuf = (nPx > 0) ? static_cast<int>(sumX / nPx) : topXBuf;

 // ── Step 3: proportional Y offset by measured height ─────────
 int headOff = 0;
 if (targetH >= cfg::head_anchor_close_min_h) {
 headOff = (targetH * cfg::head_anchor_close_pct) / 100;
 if (headOff < 3) headOff = 3;
 } else if (targetH >= cfg::head_anchor_mid_min_h) {
 headOff = (targetH * cfg::head_anchor_mid_pct) / 100;
 if (headOff < 1) headOff = 1;
 }
 // else: tiny target - leave at crown, head_off = 0.

 outDx = anchorXBuf - halfW;
 outDy = topYBuf - halfH + headOff;
 return true;
}

// ═══════════════════════════════════════════════════════════════════
// CPU image processing - runs FindTargets, then writes per-mode globals.
//
// Capture buffer width/height = MAX FOV. Each mode's per-mode FOV
// half-size is passed into FindTargets so each candidate only considers
// pixels inside its own FOV box.
//
// Targeting policy:
// - Aimbot (apply_delta_x/y) ← topmost-Y pixel + target_offset_x/y,
// restricted to ±apply_delta_fov/2.
// Head-anchored aim:
// offset_y = 0 → head crown
// offset_y = 5 → forehead / eyes (default)
// offset_y = 25 → neck / upper chest
// offset_y = 50 → center mass
// - Silent (mode_a_x/y) ← topmost-Y (or closest if
// mode_a_head_targeting=false) +
// mode_a_target_offset_x/y, restricted
// to ±mode_a_fov/2. One-click design.
// - Flicker (nonmode_a_x/y) ← same anchor logic as silent,
// restricted to ±nonmode_a_fov/2.
// Reuses mode_a_target_offset_x/y
// (matches legacy behaviour where
// flicker read mode_a_x/y directly).
// - Trigger (Otrigger_action) - reads colour directly at the crosshair,
// independent of these anchors.
// ═══════════════════════════════════════════════════════════════════
static void OptimizedProcessImage(BYTE* screenData, int w, int h) {
 const int midX = Width / 2;
 const int midY = Height / 2;

 // Dead body filter state (silent only, persists across calls).
 static int dbf_prev_dy = 0;
 static bool dbf_valid = false;

 ModeCandidate aimbot, silent, flicker;

 // Half-FOV for each mode (0 ⇒ mode inactive ⇒ skip its candidate).
 const int aimbotHalf = cfg::apply_delta_ativo ? cfg::apply_delta_fov / 2 : 0;
 const int silentHalf = cfg::mode_a_ativo ? cfg::mode_a_fov / 2 : 0;
 const int flickerHalf = cfg::nonmode_a_ativo ? cfg::nonmode_a_fov / 2 : 0;

 FindTargets(screenData, w, h,
 aimbotHalf, aimbot,
 silentHalf, silent,
 flickerHalf, flicker);

 // ── Cluster validation per mode ─────────────────────────────────
 // Each mode's closest pixel is checked independently: if a mode's
 // closest is isolated single-pixel noise, drop ONLY that mode's
 // result. Other modes (possibly looking at a different / cleaner
 // region) are unaffected.
 if (cfg::min_cluster_size > 0) {
 if (aimbot.found &&
 CountNeighbours(screenData, w, h, aimbot.bestCDx, aimbot.bestCDy)
 < cfg::min_cluster_size) {
 aimbot.found = false;
 }
 if (silent.found &&
 CountNeighbours(screenData, w, h, silent.bestCDx, silent.bestCDy)
 < cfg::min_cluster_size) {
 silent.found = false;
 }
 if (flicker.found &&
 CountNeighbours(screenData, w, h, flicker.bestCDx, flicker.bestCDy)
 < cfg::min_cluster_size) {
 flicker.found = false;
 }
 }

 // ── Aimbot output ───────────────────────────────────────────────
 if (aimbot.found) {
 apply_delta_x = aimbot.bestTDx + cfg::target_offset_x;
 apply_delta_y = aimbot.bestTDy + cfg::target_offset_y;
 oX = apply_delta_x + midX;
 oY = apply_delta_y + midY;
 } else {
 apply_delta_x = 0; apply_delta_y = 0;
 oX = midX; oY = midY;
 }

 const int stride = w * 4;

 // ── Silent output (+ dead-body filter) ──────────────────────────
 if (silent.found) {
 int aim_dx, aim_dy;
 if (cfg::mode_a_head_targeting) {
 // Topmost-Y anchor, optionally refined with shoulder-band X
 // and proportional Y offset (driven by head_anchor_proportional).
 RefineHeadAnchor(screenData, w, h, stride, silent, silentHalf,
 aim_dx, aim_dy);
 } else {
 // Legacy: closest-to-centre pixel.
 aim_dx = silent.bestCDx;
 aim_dy = silent.bestCDy;
 }

 // Default OFF in cfg - opt-in. When enabled, suppresses silent
 // aim if y-delta between frames > threshold (kills ragdoll false
 // triggers but can also reject legit peeking/jumping targets).
 if (cfg::dead_body_filter && dbf_valid &&
 FastAbs(aim_dy - dbf_prev_dy) > cfg::dead_body_threshold) {
 mode_a_x = 0; mode_a_y = 0;
 dbf_prev_dy = aim_dy;
 } else {
 mode_a_x = aim_dx + cfg::mode_a_target_offset_x;
 mode_a_y = aim_dy + cfg::mode_a_target_offset_y;
 dbf_prev_dy = aim_dy;
 dbf_valid = true;
 }
 } else {
 mode_a_x = 0; mode_a_y = 0;
 dbf_valid = false;
 }

 // ── Flicker output ──────────────────────────────────────────────
 if (flicker.found) {
 int aim_dx, aim_dy;
 if (cfg::mode_a_head_targeting) {
 RefineHeadAnchor(screenData, w, h, stride, flicker, flickerHalf,
 aim_dx, aim_dy);
 } else {
 aim_dx = flicker.bestCDx;
 aim_dy = flicker.bestCDy;
 }
 nonmode_a_x = aim_dx + cfg::mode_a_target_offset_x;
 nonmode_a_y = aim_dy + cfg::mode_a_target_offset_y;
 } else {
 nonmode_a_x = 0; nonmode_a_y = 0;
 }
}

// ═══════════════════════════════════════════════════════════════════
// GPU result processing - applies GPU dx/dy to globals.
//
// The HLSL compute shader (see ScreenCapture.cpp g_csHLSL) only outputs
// the SINGLE closest-to-center target pixel - it has no topmost-Y
// tracking and no per-mode candidates. Under the MAX-FOV architecture
// the capture box may be larger than aimbot's / silent's / flicker's
// individual FOVs, so we filter the GPU's single result against each
// mode's FOV here: a mode whose box doesn't contain the result gets
// 0/0 (no fire).
//
// Limitation: when multiple targets exist and the closest-overall
// happens to fall outside (say) aimbot's FOV but a different target
// IS inside it, aimbot still gets 0/0 because the GPU never reported
// the second target. This is fundamental to the single-result shader;
// fixing it requires extending the shader to emit per-mode results.
// Recommend keeping cfg::use_gpu_processing = false on multi-target
// engagements until the shader is extended.
// ═══════════════════════════════════════════════════════════════════
static void ProcessGPUResult(int raw_dx, int raw_dy, bool found) {
 const int midX = Width / 2;
 const int midY = Height / 2;

 if (!found) {
 apply_delta_x = 0; apply_delta_y = 0;
 oX = midX; oY = midY;
 mode_a_x = 0; mode_a_y = 0;
 nonmode_a_x = 0; nonmode_a_y = 0;
 return;
 }

 const int adx = FastAbs(raw_dx);
 const int ady = FastAbs(raw_dy);

 const int aimbotHalf = cfg::apply_delta_ativo ? cfg::apply_delta_fov / 2 : 0;
 const int silentHalf = cfg::mode_a_ativo ? cfg::mode_a_fov / 2 : 0;
 const int flickerHalf = cfg::nonmode_a_ativo ? cfg::nonmode_a_fov / 2 : 0;

 if (aimbotHalf > 0 && adx <= aimbotHalf && ady <= aimbotHalf) {
 apply_delta_x = raw_dx + cfg::target_offset_x;
 apply_delta_y = raw_dy + cfg::target_offset_y;
 oX = apply_delta_x + midX;
 oY = apply_delta_y + midY;
 } else {
 apply_delta_x = 0; apply_delta_y = 0;
 oX = midX; oY = midY;
 }

 if (silentHalf > 0 && adx <= silentHalf && ady <= silentHalf) {
 mode_a_x = raw_dx + cfg::mode_a_target_offset_x;
 mode_a_y = raw_dy + cfg::mode_a_target_offset_y;
 } else {
 mode_a_x = 0; mode_a_y = 0;
 }

 if (flickerHalf > 0 && adx <= flickerHalf && ady <= flickerHalf) {
 nonmode_a_x = raw_dx + cfg::mode_a_target_offset_x;
 nonmode_a_y = raw_dy + cfg::mode_a_target_offset_y;
 } else {
 nonmode_a_x = 0; nonmode_a_y = 0;
 }
}

// ═══════════════════════════════════════════════════════════════════
// Triggerbot - reads from aimbot's captured buffer (CPU path) or
// falls back to separate capture (GPU path, unreliable)
// ═══════════════════════════════════════════════════════════════════
static void Otrigger_action(const BYTE* aimData = nullptr,
 int aimW = 0, int aimH = 0)
{
 static int pixel_sens = 90;
 static COLORREF pixel_color = 0;
 static int lastColorMode = -1;
 static bool has_shot = false;

 if (cfg::color_mode != lastColorMode) {
 switch (cfg::color_mode) {
 case 0: case 1: pixel_color = RGB(235, 105, 254); break;
 case 2: pixel_color = RGB(255, 255, 85); break;
 case 3: pixel_color = RGB(254, 99, 106); break;
 }
 lastColorMode = cfg::color_mode;
 }

 bool key_is_down = GetAsyncKeyState(cfg::trigger_action_key) & 0x8000;
 if (!key_is_down) { has_shot = false; return; }
 if (!cfg::trigger_action_ativo || has_shot) return;

 // Don't fire if player is already holding left click (matches original)
 if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return;

 int trigW = cfg::trigger_action_fovX * 2;
 int trigH = cfg::trigger_action_fovY * 2;

 int targetR = GetRValue(pixel_color);
 int targetG = GetGValue(pixel_color);
 int targetB = GetBValue(pixel_color);

 const BYTE* scanData = nullptr;
 int dataStride = 0;
 int offX = 0, offY = 0; // trigger-region origin in buffer

 if (aimData && aimW >= trigW && aimH >= trigH) {
 // CPU path: trigger region is at center of aimbot buffer
 scanData = aimData;
 dataStride = aimW * 4;
 offX = (aimW - trigW) / 2;
 offY = (aimH - trigH) / 2;
 } else {
 // GPU path fallback: separate capture (may DXGI-timeout)
 if (!InitializeOptimizedCapture()) return;
 int leftbound = Width / 2 - cfg::trigger_action_fovX;
 int topbound = Height / 2 - cfg::trigger_action_fovY;
 BYTE* capBuf = nullptr;
 if (!g_optimizedCapture->CaptureRegionAdaptive(
 leftbound, topbound, trigW, trigH, &capBuf))
 return;
 scanData = capBuf;
 dataStride = trigW * 4;
 offX = 0;
 offY = 0;
 }

 const int centerX = trigW >> 1;
 const int centerY = trigH >> 1;
 const int maxRadius = FastMin(trigW, trigH) >> 1;

 // Inline color-match against the configured trigger color, with
 // the per-channel ±pixel_sens tolerance.
 auto matchAt = [&](int tx, int ty) -> bool {
 if (tx < 0 || tx >= trigW || ty < 0 || ty >= trigH) return false;
 const BYTE* px = scanData + (offY + ty) * dataStride
 + (offX + tx) * 4;
 return FastAbs(px[2] - targetR) < pixel_sens &&
 FastAbs(px[1] - targetG) < pixel_sens &&
 FastAbs(px[0] - targetB) < pixel_sens;
 };

 if (cfg::trigger_polygon_check) {
 // ── 4-ray crossing test (cv::pointPolygonTest analogue) ─────
 // Crosshair is treated as "inside" a purple region iff a ray
 // cast from centre in each of +X, -X, +Y, -Y directions hits
 // at least one purple pixel within trigger FOV.
 //
 // Why: an enemy outline is a closed-ish ring of purple around
 // the model. When the crosshair sits inside that ring, all
 // four rays cross the outline within a short radius. A
 // single-sided UI element (HP bar, muzzle flash, ability icon)
 // only puts purple on ONE side of the crosshair - at most 2
 // rays hit → no fire. Crosshair just outside an enemy outline
 // also fails (the away-side rays escape into empty space).
 //
 // Short-circuits as soon as all four rays have hit, so the
 // expected cost on a real "inside enemy" frame is ~2-4 pixel
 // probes regardless of trigger FOV size.
 bool hitPlusX = false, hitMinusX = false;
 bool hitPlusY = false, hitMinusY = false;

 for (int r = 1; r <= maxRadius; ++r) {
 if (!hitPlusX && matchAt(centerX + r, centerY)) hitPlusX = true;
 if (!hitMinusX && matchAt(centerX - r, centerY)) hitMinusX = true;
 if (!hitPlusY && matchAt(centerX, centerY + r)) hitPlusY = true;
 if (!hitMinusY && matchAt(centerX, centerY - r)) hitMinusY = true;
 if (hitPlusX && hitMinusX && hitPlusY && hitMinusY) break;
 }

 if (hitPlusX && hitMinusX && hitPlusY && hitMinusY) {
 sendCommand(0, 0, 'L');
 has_shot = true;
 }
 }
 else {
 // ── Legacy spiral-first-hit (kept for compatibility) ────────
 // Fires on ANY purple pixel within trigger FOV - fast but
 // false-fires on isolated UI purples.
 for (int r = 0; r < maxRadius; r += 2) {
 for (int dx = -r; dx <= r; dx += 2) {
 if (matchAt(centerX + dx, centerY - r) ||
 matchAt(centerX + dx, centerY + r)) {
 sendCommand(0, 0, 'L');
 has_shot = true; return;
 }
 }
 for (int dy = -r + 1; dy <= r - 1; dy += 2) {
 if (matchAt(centerX - r, centerY + dy) ||
 matchAt(centerX + r, centerY + dy)) {
 sendCommand(0, 0, 'L');
 has_shot = true; return;
 }
 }
 }
 }
}

// ═══════════════════════════════════════════════════════════════════
// MAX-FOV computation - capture region size = max of all active modes'
// configured FOVs. Recomputed every iter so web-UI FOV / activation
// toggles propagate to the capture region on the very next frame.
// ═══════════════════════════════════════════════════════════════════
static int ComputeMaxFov() {
 int m = 0;
 if (cfg::apply_delta_ativo && cfg::apply_delta_fov > m) m = cfg::apply_delta_fov;
 if (cfg::apply_deltaassist_ativo && cfg::apply_deltaassist_fov > m) m = cfg::apply_deltaassist_fov;
 if (cfg::mode_a_ativo && cfg::mode_a_fov > m) m = cfg::mode_a_fov;
 if (cfg::nonmode_a_ativo && cfg::nonmode_a_fov > m) m = cfg::nonmode_a_fov;
 if (m <= 0) m = 200; // safety fallback when nothing active
 return m;
}

// ═══════════════════════════════════════════════════════════════════
// Main capture loop - branches GPU / CPU based on cfg toggle
// ═══════════════════════════════════════════════════════════════════
void CaptureScreen() {
 if (!InitializeOptimizedCapture()) return;

 // Elevate this thread's priority for lowest latency
 set_thread_high_priority();

 static thread_local bool moved_mouse = false;

 // DXGI paces the loop via AcquireNextFrame. 1ms max-wait keeps the
 // loop hot - on any refresh rate ≥ 60Hz a new frame is delivered well
 // within the timeout (and the kernel block costs ~µs on no-frame).
 // Tighter timeout means silent-aim threads see a fresh capture_seq
 // bump within ~1ms of the actual game render, minimizing stale-coord
 // risk on one-click trigger.
 constexpr UINT CAPTURE_TIMEOUT_MS = 1;

 while (true) {
 // Recompute MAX-FOV each iter so config changes via web UI
 // (FOV sliders, mode toggles) take effect on the very next
 // frame without any signalling / mutex hand-off. Each mode's
 // per-FOV filtering happens inside OptimizedProcessImage on
 // this same frame.
 int w = ComputeMaxFov();
 int h = w;
 {
 std::lock_guard<std::mutex> lock(fovMutex);
 currentFOV = w;
 }

 if (w <= 0 || h <= 0) {
 std::this_thread::sleep_for(std::chrono::milliseconds(10));
 continue;
 }

 int captureX = Width / 2 - (w / 2);
 int captureY = Height / 2 - (h / 2);
 bool captured = false;

 BYTE* screenData = nullptr; // valid when CPU path succeeds
 int capW = 0, capH = 0;

 // ────────────────── GPU PATH ──────────────────
 if (cfg::use_gpu_processing) {
 int dx, dy;
 bool found;
 if (g_optimizedCapture->CaptureRegionGPU(
 captureX, captureY, w, h, dx, dy, found, CAPTURE_TIMEOUT_MS))
 {
 ProcessGPUResult(dx, dy, found);
 captured = true;
 }
 // GPU failed (FOV > 255 or init) - fall back to CPU
 if (!captured) {
 if (g_optimizedCapture->CaptureRegionAdaptive(
 captureX, captureY, w, h, &screenData, CAPTURE_TIMEOUT_MS))
 {
 OptimizedProcessImage(screenData, w, h);
 captured = true;
 capW = w; capH = h;
 }
 }
 }
 // ────────────────── CPU PATH ──────────────────
 else {
 if (g_optimizedCapture->CaptureRegionAdaptive(
 captureX, captureY, w, h, &screenData, CAPTURE_TIMEOUT_MS))
 {
 OptimizedProcessImage(screenData, w, h);
 captured = true;
 capW = w; capH = h;
 }
 }

 if (captured) {
 // Publish fresh frame to silent-aim / flicker threads.
 // Bump BEFORE running aimbot/trigger - the globals
 // (mode_a_x/y, apply_delta_x/y) were written inside
 // OptimizedProcessImage / ProcessGPUResult above, so they
 // are stable by this point. Threads polling capture_seq
 // can wake up the moment new coords are visible.
 //
 // Store fov-used BEFORE the seq bump so its value is
 // visible to any thread that sees the bumped seq via
 // acquire-load (release ordering on the seq store
 // synchronises-with the acquire load).
 capture_fov_used.store(w, std::memory_order_relaxed);
 capture_seq.fetch_add(1, std::memory_order_release);

 if (moved_mouse) {
 apply_delta(apply_delta_x, apply_delta_y, cfg::apply_delta_smooth);
 Magnet(apply_delta_x, apply_delta_y, cfg::apply_deltaassist_smooth);
 }
 Otrigger_action(screenData, capW, capH);
 }

 moved_mouse = true;

 // No manual frame pacing - AcquireNextFrame's tight 1ms timeout
 // keeps capture rate at the game's render cadence (or the
 // monitor's refresh, whichever is lower) without burning CPU.
 }
}
