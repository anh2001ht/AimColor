#include "Config.h"

namespace cfg
{
 int color_mode = 0;

 bool apply_delta_ativo = true;
 int apply_deltakey1 = VK_XBUTTON1;
 int apply_deltakey2 = VK_SHIFT;
 int target_offset_x = 1;
 int target_offset_y = 5;
 int apply_delta_fov = 82;
 float apply_delta_smooth = 1.4f;
 float speed = 0.4f;
 int sleep = 0;

 bool apply_deltaassist_ativo = false;
 int assist_apply_deltakey = VK_MENU;
 int assist_target_offset_x = 2;
 int assist_target_offset_y = 3;
 int apply_deltaassist_fov = 1;
 float apply_deltaassist_smooth = 1.5f;
 float assist_speed = 1.0f;
 int assist_sleep = 0;

 bool nonmode_a_ativo = false;
 int nonmode_a_key = VK_XBUTTON2;
 int nonmode_a_fov = 100;
 int nonmode_a_delay_between_shots = 1;
 float nonmode_a_distance = 2.5f;

 bool mode_a_ativo = true;
 int mode_a_key = VK_XBUTTON2;
 int mode_a_target_offset_x = 0;
 int mode_a_target_offset_y = 3;
 int mode_a_fov = 100;
 int mode_a_delay_between_shots = 1;
 float distance = 2.62f;

 bool mode_a_head_targeting = true; // scan upward to find head for silent aim

 // Arduino-side P cooldown. Lower = faster spam, higher = stricter
 // one-shot pacing. 50ms supports 20Hz key spam (enough for most
 // engagement patterns) while still preventing accidental
 // double-fires on a single intended press.
 int mode_a_cooldown_ms = 50;

 bool useIstrigFilter = true;

 bool trigger_action_ativo = true;
 int trigger_action_key = VK_MENU;
 int trigger_action_delay = 1;
 int trigger_action_fovX = 3;
 int trigger_action_fovY = 3;

 int menorRGB[3] = { 70, 0, 120 };
 int maiorRGB[3] = { 255, 190, 255 };
 int menorHSV[3] = { 270, 38, 40 };
 int maiorHSV[3] = { 310, 100, 100 };

 int ngrok = 1;

 bool use_gpu_processing = false;

 // Default OFF - opt-in. When ON, suppresses silent aim if Y-delta
 // between frames exceeds threshold (kills false-fire on ragdoll
 // corpses). Can suppress legit shots on peeking/jumping targets,
 // so leaving default OFF preserves one-click-one-kill reliability.
 bool dead_body_filter = false;
 int dead_body_threshold = 15;

 // Default 2 (was 3) - partial-visibility targets (sliver-peeks
 // showing 1-2 head pixels) won't be rejected as noise. 0 disables.
 int min_cluster_size = 2;

 // 4-ray polygon test on trigger. Default ON - rejects most
 // UI / HUD purple false fires. See Config.h for full rationale.
 bool trigger_polygon_check = true;

 // Distance-aware aimbot smoothing - defaults ported from
 // Json's Colorbot reference, tuned for Valorant target sizes.
 // < 10 px → 0.4× speed (fine micro-correction)
 // < 30 px → 0.7× speed (medium-range tracking)
 // ≥ 30 px → 1.0× (full speed snap toward target)
 bool apply_delta_dist_smoothing = true;
 int apply_delta_near_dist = 10;
 int apply_delta_mid_dist = 30;
 float apply_delta_near_mult = 0.4f;
 float apply_delta_mid_mult = 0.7f;

 // Head anchor refinement - defaults ported from tfirm reference.
 // 18% of height ≥ 30 px tall (close enemy) → forehead/eyes
 // 10% of height ≥ 10 px tall (mid enemy) → just below crown
 // smaller targets get 0 offset (aim crown directly)
 bool head_anchor_proportional = true;
 int head_anchor_band_rows = 0; // 0 ⇒ auto (height/4 clamped 2..6)
 int head_anchor_gap_tolerance = 2;
 int head_anchor_close_pct = 18;
 int head_anchor_mid_pct = 10;
 int head_anchor_close_min_h = 30;
 int head_anchor_mid_min_h = 10;
}
