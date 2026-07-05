#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <mutex>
#include <windows.h>

#include "core/Config.h"
#include "core/Globals.h"
#include "core/ConfigManager.h"
#include "security/LicenseManager.h"
#include "network/UDPClient.h"
#include "network/WebServer.h"
#include "capture/CaptureLoop.h"
#include "capture/ColorDetector.h" // MonitorInfo::GetRefreshRate
#include "input/MouseMove.h"
#include "util/SystemUtils.h"
#include "xorstr.h"
#include "VMProtectSDK.h"

#pragma comment(lib, "d3d11.lib")

static std::mt19937 mt{ std::random_device{}() };

int get_random_int(int min, int max) {
 std::uniform_int_distribution<int> dist{ min, max };
 return dist(mt);
}

// ============================================================================
// Compute fresh-capture wait timeout - 2 frame-intervals + 2ms margin.
// Floored at 6ms (kernel spin + 100µs poll budget + safety).
//
// Examples:
// 60 Hz → 35 ms 144 Hz → 16 ms
// 120 Hz → 18 ms 200 Hz → 12 ms
// 240 Hz → 10 ms
// 360 Hz → 7 ms
// 500 Hz → 6 ms (floored)
//
// Why 2 frames:
// Frame 1 may have been IN-FLIGHT when the user pressed silent - its
// AcquireNextFrame already grabbed a desktop frame rendered BEFORE
// the user reacted to the target. Frame 2 is the first one fully
// started AFTER the press, so it's the first one that can actually
// see what the user saw when they clicked. Capping at 2 frames
// stays comfortably below human "click felt like miss" perception
// while reliably catching targets that appear right at the press
// moment - the core Valorant jiggle-peek case.
// ============================================================================
static int ComputeFreshTimeoutMs() {
 int refresh = MonitorInfo::GetRefreshRate();
 if (refresh <= 0) refresh = 60; // fallback if EnumDisplaySettings fails
 int t = (2 * 1000 / refresh) + 2;
 if (t < 6) t = 6;
 return t;
}

// ============================================================================
// Wait for a capture frame that actually contains target coordinates.
//
// Reads `coordsX` / `coordsY` references - passed by the caller as the
// per-mode coord globals (mode_a_x/y for silent, nonmode_a_x/y for
// flicker).
//
// Loops across MULTIPLE seq advances (not just one) - the first new
// frame after the key-edge may have grabbed a desktop snapshot rendered
// BEFORE the target was visible, in which case its mode coords are 0
// even though the frame is "fresh." We keep waiting until either:
// - coords are non-zero (definitive hit - fire),
// - timeoutMs elapsed (definitive miss - bail, no fire).
//
// Snapshots coords into outX/outY at the same acquire-load that
// validates the seq, so capture iters that fire AFTER the validation
// can't overwrite the value the caller will fire on.
//
// Under the MAX-FOV-plus-filter architecture (capture always at
// max-of-active-modes FOV, each mode's per-FOV mask applied during
// FindTargets), there is no FOV race - coords from any fresh frame
// are immediately authoritative for that mode.
// ============================================================================
static bool WaitForFreshCapture(uint64_t seqBefore, int timeoutMs,
 const int& coordsX, const int& coordsY,
 int& outX, int& outY)
{
 auto start = std::chrono::high_resolution_clock::now();
 while (true) {
 uint64_t curSeq = capture_seq.load(std::memory_order_acquire);
 if (curSeq != seqBefore) {
 // Release-acquire synchronises-with capture's seq publish:
 // per-mode coord writes are visible and coherent with curSeq.
 int x = coordsX;
 int y = coordsY;
 if (x != 0 || y != 0) {
 outX = x;
 outY = y;
 return true;
 }
 // Fresh frame, target genuinely not visible in this frame's
 // desktop snapshot. Continue waiting in case it appears in
 // a subsequent frame within the timeout window.
 seqBefore = curSeq;
 }

 auto now = std::chrono::high_resolution_clock::now();
 if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeoutMs) {
 outX = 0;
 outY = 0;
 return false;
 }

 std::this_thread::sleep_for(std::chrono::microseconds(100));
 }
}

// ============================================================================
// Mode A (Silent Aim) - Latch system (fires once per press).
// On key edge:
// 1. Sample capture_seq.
// 2. Fast path - mode_a_x/y from previous frame already non-zero →
// fire instantly. Per-mode filtering in the capture thread means
// these coords are guaranteed to lie within mode_a_fov already.
// 3. Slow path - previous frame had no target visible inside
// mode_a_fov → wait (up to 2 frame intervals) for a subsequent
// capture to publish non-zero coords, or bail on timeout.
// WaitForFreshCapture snapshots the coords atomically with the
// seq validation so capture iters that fire AFTER validation
// can't overwrite them.
// Runs at THREAD_PRIORITY_HIGHEST to minimize key-detection latency.
// ============================================================================
void mode_a() {
 ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

 bool key_was_down = false;
 auto lastFireTime = std::chrono::high_resolution_clock::now();
 // Debounce window: minimal - guards against physical key bounce only.
 // Firmware v2 preserves real button state via `real | MOUSE_LEFT` in
 // every HID report, so injected P commands no longer cause false
 // release/press edges on the activation key. Rapid-fire spam is
 // gated by the Arduino's K cooldown, not by this debounce.
 constexpr auto DEBOUNCE = std::chrono::milliseconds(20);
 // Refresh-rate-adaptive timeout - see ComputeFreshTimeoutMs comment.
 const int FRESH_TIMEOUT_MS = ComputeFreshTimeoutMs();

 while (true) {
 if (cfg::mode_a_ativo) {
 bool key_is_down = GetAsyncKeyState(cfg::mode_a_key) & 0x8000;
 auto now = std::chrono::high_resolution_clock::now();

 if (key_is_down && !key_was_down &&
 (now - lastFireTime) > DEBOUNCE)
 {
 uint64_t seqBefore = capture_seq.load(std::memory_order_acquire);

 int mx = mode_a_x;
 int my = mode_a_y;
 if (mx == 0 && my == 0) {
 WaitForFreshCapture(seqBefore, FRESH_TIMEOUT_MS,
 mode_a_x, mode_a_y, mx, my);
 }

 if (mx != 0 || my != 0) {
 SnapShoot_P(mx, my);
 lastFireTime = std::chrono::high_resolution_clock::now();
 }
 }
 key_was_down = key_is_down;
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}

// ============================================================================
// Non-mode A (Flicker) - Latch system.
// Same head-top one-click semantics as silent aim, but reads from
// flicker's per-mode filtered coords (nonmode_a_x/y) - populated by
// the capture thread using nonmode_a_fov as the FOV mask. Differs
// from silent only in the Arduino command sent ('F' instead of 'P')
// and the FOV its coords were filtered by.
// ============================================================================
void nonmode_a() {
 ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

 bool key_was_down = false;
 auto lastFireTime = std::chrono::high_resolution_clock::now();
 constexpr auto DEBOUNCE = std::chrono::milliseconds(20);
 const int FRESH_TIMEOUT_MS = ComputeFreshTimeoutMs();

 while (true) {
 if (cfg::nonmode_a_ativo) {
 bool key_is_down = GetAsyncKeyState(cfg::nonmode_a_key) & 0x8000;
 auto now = std::chrono::high_resolution_clock::now();

 if (key_is_down && !key_was_down &&
 (now - lastFireTime) > DEBOUNCE)
 {
 uint64_t seqBefore = capture_seq.load(std::memory_order_acquire);

 int mx = nonmode_a_x;
 int my = nonmode_a_y;
 if (mx == 0 && my == 0) {
 WaitForFreshCapture(seqBefore, FRESH_TIMEOUT_MS,
 nonmode_a_x, nonmode_a_y, mx, my);
 }

 if (mx != 0 || my != 0) {
 SnapShoot_F(mx, my);
 lastFireTime = std::chrono::high_resolution_clock::now();
 }
 }
 key_was_down = key_is_down;
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}

// ============================================================================
// Admin check
// ============================================================================
bool IsAdmin() {
 SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
 PSID AdminGroup;
 BOOL bResult = AllocateAndInitializeSid(
 &NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdminGroup);
 if (!bResult) return false;

 BOOL isAdmin = FALSE;
 bResult = CheckTokenMembership(NULL, AdminGroup, &isAdmin);
 FreeSid(AdminGroup);
 return bResult && isAdmin;
}

// ============================================================================
// Initial FOV selection
// ============================================================================
static void InitFOV()
{
 int best = 0;
 if (cfg::apply_delta_ativo && cfg::apply_delta_fov > best)
 best = cfg::apply_delta_fov;
 if (cfg::apply_deltaassist_ativo && cfg::apply_deltaassist_fov > best)
 best = cfg::apply_deltaassist_fov;
 if (cfg::mode_a_ativo && cfg::mode_a_fov > best)
 best = cfg::mode_a_fov;
 if (cfg::nonmode_a_ativo && cfg::nonmode_a_fov > best)
 best = cfg::nonmode_a_fov;
 if (best <= 0)
 best = 200;

 std::lock_guard<std::mutex> lock(fovMutex);
 currentFOV = best;
}

// ============================================================================
// Entry point
// ============================================================================
int main()
{
 // ── DPI awareness ───────────────────────────────────────────────
 // Force physical-pixel coordinates regardless of OS display scaling.
 // Without this:
 // - GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN) returns LOGICAL dims
 // (e.g. 2560×1440 on a 4K@150%-scaled display).
 // - DXGI Desktop Duplication returns PHYSICAL dims (3840×2160).
 // - Crop region computed from logical Width/Height → wrong region
 // captured → silent aim aims at wrong screen area.
 //
 // Per-monitor V2 awareness (Win10 1703+) handles mixed-DPI multi-mon.
 // Falls back to legacy SetProcessDPIAware on older systems.
 if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
 SetProcessDPIAware();
 }

 // Defense-in-depth: even if the loader passed the gate, re-validate the
 // license inside the hollowed payload. Silent exit (no cout - Windows
 // subsystem has no console; the loader already showed any error UI).
 if (!LicenseManager::checkLicense()) {
 std::exit(1);
 }

 std::string arduino_ip;
 int arduino_port;

 if (ConfigManager::loadIPAndPort(arduino_ip, arduino_port)) {
 ConfigManager::loadConfig();
 }
 else {
 // No data file yet, fall back to placeholder defaults that match
 // the firmware's static IP + the mDNS-shaped wire protocol port.
 // Update via the WebUI Performance tab once running.
 arduino_ip = xorstr_("192.168.1.216");
 arduino_port = 5353;
 ConfigManager::saveConfig(arduino_ip, arduino_port);
 }

 initializeUDPSocket(arduino_ip.c_str(), arduino_port);

 if (!ConfigManager::loadConfig()) {
 ConfigManager::saveConfig();
 }

 std::atexit(cleanupSocket);
 sendCommand(30, 30, 'M');

 // Push cooldown config to Arduino on startup.
 // Note: split-delay (`D` command) was removed in firmware v2 - the
 // P command now uses a deterministic 4-report sequence
 // (move, press, release, snapback) with no artificial delay between
 // reports. See coloruino-fw.ino for rationale.
 sendArduinoConfig('K', cfg::mode_a_cooldown_ms);

 ::FreeConsole();

 // ── Performance: high priority + 0.5ms timer resolution ──
 set_process_priority(HIGH_PRIORITY_CLASS);
 set_timer_resolution();

 Width = GetSystemMetrics(SM_CXSCREEN);
 Height = GetSystemMetrics(SM_CYSCREEN);

 InitFOV();

 std::thread(CaptureScreen).detach();
 std::thread(mode_a).detach();
 std::thread(nonmode_a).detach();

 initializeNetworking(13548);

 while (true) {
 std::this_thread::sleep_for(std::chrono::hours(1));
 }
}
