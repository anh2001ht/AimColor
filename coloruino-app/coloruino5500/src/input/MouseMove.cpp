#include "MouseMove.h"
#include "core/Config.h"
#include "core/Globals.h"
#include "network/UDPClient.h"

#include <windows.h>
#include <mutex>
#include <cmath>

static bool key_ativa = false;

// ============================================================================
// apply_delta - Aimbot (Per-Frame Smoothing with overflow accumulation)
//
// NOTE: This function intentionally does NOT modify currentFOV. Under the
// MAX-FOV-plus-filter architecture (see Globals.h), the capture thread
// owns currentFOV and recomputes it every iter as max-of-active-modes.
// Per-mode FOV masking (only consider pixels within ±apply_delta_fov/2
// of screen centre) is applied inside FindTargets - aimbot just consumes
// the pre-filtered apply_delta_x/y written by OptimizedProcessImage.
// ============================================================================
void apply_delta(int deltaX, int deltaY, double smooth)
{
 static double overflow_x = 0.0, overflow_y = 0.0;

 if (!cfg::apply_delta_ativo) { overflow_x = overflow_y = 0; return; }
 if (!GetAsyncKeyState(cfg::apply_deltakey1) && !GetAsyncKeyState(cfg::apply_deltakey2)) {
 overflow_x = overflow_y = 0;
 return;
 }
 if (deltaX == 0 && deltaY == 0) { overflow_x = overflow_y = 0; return; }

 if (smooth < 1.0) smooth = 1.0;
 double speed_mult = cfg::speed > 0.0 ? cfg::speed : 1.0;

 // ── Distance-aware smoothing ────────────────────────────────────
 // Apply an extra multiplier whose magnitude depends on |delta|.
 // Closer targets get a smaller multiplier → finer movement per
 // frame → less overshoot when settling on a micro-correction.
 // Far targets stay at 1.0 → snap toward target at full speed.
 // Use squared distance to skip the sqrt (compare against squared
 // thresholds).
 double dist_factor = 1.0;
 if (cfg::apply_delta_dist_smoothing) {
 const double dist2 = (double)deltaX * deltaX + (double)deltaY * deltaY;
 const double nearD = (double)cfg::apply_delta_near_dist;
 const double midD = (double)cfg::apply_delta_mid_dist;
 if (dist2 < nearD * nearD) dist_factor = cfg::apply_delta_near_mult;
 else if (dist2 < midD * midD) dist_factor = cfg::apply_delta_mid_mult;
 if (dist_factor <= 0.0) dist_factor = 1.0; // safety against misconfig
 }

 // Accumulate fractional movement so sub-pixel deltas are never lost
 double exact_x = (deltaX / smooth) * speed_mult * dist_factor + overflow_x;
 double exact_y = (deltaY / smooth) * speed_mult * dist_factor + overflow_y;

 int moveX = static_cast<int>(exact_x);
 int moveY = static_cast<int>(exact_y);

 overflow_x = exact_x - moveX;
 overflow_y = exact_y - moveY;

 if (moveX != 0 || moveY != 0) {
 sendCommand(moveX, moveY, 'M');
 }
}

// ============================================================================
// Magnet (Assist) - Per-Frame Smoothing with overflow accumulation
//
// NOTE: Same FOV-ownership policy as apply_delta - capture thread owns
// currentFOV (= max-of-active-modes), per-mode masking happens inside
// FindTargets. Magnet reads aimbot's filtered apply_delta_x/y (legacy
// behaviour - assist tracks aimbot's target).
// ============================================================================
void Magnet(int deltaX, int deltaY, double smooth)
{
 static bool keyPressProcessed = false;
 static double overflow_x = 0.0, overflow_y = 0.0;
 bool key_down = GetAsyncKeyState(cfg::assist_apply_deltakey) & 0x8000;

 if (key_down) {
 if (!keyPressProcessed) {
 key_ativa = !key_ativa;
 keyPressProcessed = true;
 }
 }
 else {
 keyPressProcessed = false;
 }

 if (!cfg::apply_deltaassist_ativo || !key_ativa) {
 overflow_x = overflow_y = 0;
 return;
 }
 if (deltaX == 0 && deltaY == 0) { overflow_x = overflow_y = 0; return; }

 if (smooth < 1.0) smooth = 1.0;
 double speed_mult = cfg::assist_speed > 0.0 ? cfg::assist_speed : 1.0;

 double exact_x = (deltaX / smooth) * speed_mult + overflow_x;
 double exact_y = (deltaY / smooth) * speed_mult + overflow_y;

 int moveX = static_cast<int>(exact_x);
 int moveY = static_cast<int>(exact_y);

 overflow_x = exact_x - moveX;
 overflow_y = exact_y - moveY;

 if (moveX != 0 || moveY != 0) {
 sendCommand(moveX, moveY, 'M');
 }
}

// ============================================================================
// Silent Aim (mode_a) - instant, one-shot headshot
// The old normalize+clamp(10)+multiply formula algebraically reduces to
// moveX = deltaX * distance in ALL cases (the clamp is self-cancelling:
// when dist<10: (dX/dist)*(dist*m) = dX*m
// when dist≥10: (dX/10)*(10*m) = dX*m
// ) - so we skip the sqrt/normalize/clamp entirely.
//
// Does NOT modify currentFOV - under the MAX-FOV-plus-filter
// architecture (see Globals.h), currentFOV is owned by the capture
// thread and recomputed every iter as max-of-active-modes. The
// per-mode FOV mask is applied inside FindTargets, not at the capture
// region level.
// ============================================================================
void SnapShoot_P(int deltaX, int deltaY) {
 if (!cfg::mode_a_ativo) return;
 if (deltaX == 0 && deltaY == 0) return;

 float mult = cfg::distance > 0.0f ? cfg::distance : 1.0f;
 int moveX = static_cast<int>(deltaX * mult);
 int moveY = static_cast<int>(deltaY * mult);

 sendCommand(moveX, moveY, 'P');
}

// ============================================================================
// Flicker (nonmode_a)
// Same algebraic simplification as silent aim. Same FOV-ownership
// policy - currentFOV is not touched here.
// ============================================================================
void SnapShoot_F(int deltaX, int deltaY) {
 if (!cfg::nonmode_a_ativo) return;
 if (deltaX == 0 && deltaY == 0) return;

 float mult = cfg::nonmode_a_distance > 0.0f ? cfg::nonmode_a_distance : 1.0f;
 int moveX = static_cast<int>(deltaX * mult);
 int moveY = static_cast<int>(deltaY * mult);

 sendCommand(moveX, moveY, 'F');
}
