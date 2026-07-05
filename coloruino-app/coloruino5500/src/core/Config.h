#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace cfg
{
 extern int color_mode;

 extern bool apply_delta_ativo;
 extern int apply_deltakey1;
 extern int apply_deltakey2;
 extern int target_offset_x;
 extern int target_offset_y;
 extern int apply_delta_fov;
 extern float apply_delta_smooth;
 extern float speed;
 extern int sleep;

 extern bool apply_deltaassist_ativo;
 extern int assist_apply_deltakey;
 extern int assist_target_offset_x;
 extern int assist_target_offset_y;
 extern int apply_deltaassist_fov;
 extern float apply_deltaassist_smooth;
 extern float assist_speed;
 extern int assist_sleep;

 extern bool nonmode_a_ativo;
 extern int nonmode_a_key;
 extern int nonmode_a_fov;
 extern int nonmode_a_delay_between_shots;
 extern float nonmode_a_distance;

 extern bool mode_a_ativo;
 extern int mode_a_key;
 extern int mode_a_target_offset_x;
 extern int mode_a_target_offset_y;
 extern int mode_a_fov;
 extern int mode_a_delay_between_shots;
 extern float distance;

 // Head targeting: scan upward from closest pixel to find head top.
 // Only affects silent aim (mode_a) and flicker (nonmode_a).
 // Aimbot (apply_delta) still uses closest pixel for tracking.
 extern bool mode_a_head_targeting;

 // Arduino P-command cooldown in milliseconds (default 200).
 extern int mode_a_cooldown_ms;

 extern bool useIstrigFilter;

 extern bool trigger_action_ativo;
 extern int trigger_action_key;
 extern int trigger_action_delay;
 extern int trigger_action_fovX;
 extern int trigger_action_fovY;

 extern int menorRGB[3];
 extern int maiorRGB[3];
 extern int menorHSV[3];
 extern int maiorHSV[3];

 extern int ngrok;

 extern bool use_gpu_processing;

 // Dead body filtering: suppress silent aim when Y-delta between
 // frames exceeds threshold (corpse ragdoll snaps downward).
 extern bool dead_body_filter;
 extern int dead_body_threshold; // pixels - default 15

 // Minimum pixel cluster: after spiral finds closest pixel, verify
 // at least N of its 8 neighbours are also purple. Rejects single-
 // pixel noise / UI artefacts. 0 = disabled.
 extern int min_cluster_size;

 // ─── Trigger polygon check ──────────────────────────────────────
 // When true, Otrigger_action requires purple to be detected in ALL
 // FOUR cardinal directions (+X, -X, +Y, -Y) from the crosshair
 // within trigger FOV before firing. This is a coarse "is the
 // crosshair INSIDE the enemy outline" test - equivalent to the
 // cv::pointPolygonTest check used by contour-based bots, but
 // implemented directly on the LUT pixel buffer (no OpenCV).
 //
 // Effect:
 // - True (default): rejects UI purples (HP bar, ability icons,
 // muzzle flash) that only present purple on ONE side of the
 // crosshair. Also rejects fires when crosshair is at the edge
 // of an enemy outline (one ray escapes the outline ring).
 // Fewer false fires.
 // - False: legacy spiral-first-hit. Fires on ANY purple pixel
 // within trigger FOV. Faster but noisier.
 extern bool trigger_polygon_check;

 // ─── Distance-aware aimbot smoothing ────────────────────────────
 // Scales apply_delta's per-frame movement by an extra multiplier
 // whose magnitude depends on |target delta|. Closer to target =
 // smaller multiplier = finer movement = less overshoot on
 // micro-corrections.
 //
 // |delta| < apply_delta_near_dist → apply_delta_near_mult (default 0.4)
 // |delta| < apply_delta_mid_dist → apply_delta_mid_mult (default 0.7)
 // otherwise → 1.0 (no scaling)
 //
 // Affects aimbot only (apply_delta). Magnet/SnapShoot_P/F unaffected.
 extern bool apply_delta_dist_smoothing; // master enable
 extern int apply_delta_near_dist;
 extern int apply_delta_mid_dist;
 extern float apply_delta_near_mult;
 extern float apply_delta_mid_mult;

 // ─── Head anchor refinement (silent + flicker) ──────────────────
 // After FindTargets reports the topmost-Y purple pixel, refine the
 // aim anchor before applying user offsets:
 //
 // 1. Walk down from the topmost pixel at the same column,
 // tolerating up to `head_anchor_gap_tolerance` consecutive
 // non-purple pixels. Bottom of the walk = enemy bottom.
 // Difference = enemy height in pixels.
 //
 // 2. Average X across the top `head_anchor_band_rows` rows of
 // purple pixels within mode's FOV. On near targets this is
 // just the head crown; on far targets the band catches both
 // shoulder blobs and their midpoint sits dead-center under
 // the head. Replaces the topmost pixel's X (which can be one
 // shoulder when outline is partial).
 //
 // 3. Adapt the per-shot Y offset to the measured height:
 // height ≥ head_anchor_close_min_h →
 // offset = max(3, height * head_anchor_close_pct/100)
 // height ≥ head_anchor_mid_min_h →
 // offset = max(1, height * head_anchor_mid_pct/100)
 // smaller → offset = 0 (aim crown)
 // Lands the shot on forehead / eyes regardless of distance.
 //
 // User's mode_a_target_offset_x/y is still applied on top - they
 // act as a global bias relative to the refined anchor.
 //
 // Affects mode_a (silent) and nonmode_a (flicker) - both already
 // use the topmost-Y anchor. Aimbot (apply_delta) is unaffected
 // (it uses its own continuous tracking - refinement would fight
 // the smoothing).
 extern bool head_anchor_proportional; // master enable
 extern int head_anchor_band_rows; // 0 = auto (clamp(h/4, 2, 6))
 extern int head_anchor_gap_tolerance; // px allowed between purple in walk-down
 extern int head_anchor_close_pct; // % of height for close targets
 extern int head_anchor_mid_pct; // % of height for mid targets
 extern int head_anchor_close_min_h; // min height (px) to be "close"
 extern int head_anchor_mid_min_h; // min height (px) to be "mid"
}
