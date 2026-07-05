#pragma once

#include <mutex>
#include <atomic>
#include <cstdint>

extern std::mutex fovMutex;
extern int currentFOV; // captured-region size = MAX of all active modes' FOVs
extern int Width, Height;
extern int oX, oY;

// Per-mode FILTERED target coordinates. All three are written by the
// capture thread inside OptimizedProcessImage / ProcessGPUResult, in
// the same single linear pass over the capture buffer:
//
// apply_delta_x/y - aimbot. Topmost-Y purple pixel within
// ±(apply_delta_fov/2) of screen centre.
// mode_a_x/y - silent. Topmost-Y (or closest, depending on
// mode_a_head_targeting) within
// ±(mode_a_fov/2) of screen centre.
// nonmode_a_x/y - flicker. Same anchor logic as silent but
// filtered by ±(nonmode_a_fov/2).
//
// Splitting per-mode coords means EACH mode sees only targets inside
// its OWN configured FOV - the capture thread does the filtering
// once per frame, so the consumer threads (apply_delta, mode_a,
// nonmode_a) just read their own field with zero coordination.
// Eliminates the legacy "shrink currentFOV per-mode" race entirely.
extern int apply_delta_x, apply_delta_y;
extern int mode_a_x, mode_a_y;
extern int nonmode_a_x, nonmode_a_y;

// Frame sequence counter - incremented by capture thread after each
// successful frame is processed and globals (mode_a_x/y, apply_delta_x/y,
// nonmode_a_x/y) are written. Silent aim / flicker threads sample
// this on key-edge, then wait for it to advance before reading coords - 
// guarantees the shot fires using fresh capture data, not stale coords
// from a frame captured before the user pressed the key.
extern std::atomic<uint64_t> capture_seq;

// FOV used by the most recently completed capture iteration. Under
// the MAX-FOV-plus-filter architecture this always equals currentFOV
// (= MAX of active modes' FOVs). Kept for debugging / future use.
extern std::atomic<int> capture_fov_used;
