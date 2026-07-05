// winsock2 MUST come before windows.h to avoid winsock.h conflict
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <netfw.h>

#include "WebServer.h"
#include "UDPClient.h"
#include "security/AntiDebug.h"
#include "security/Auth.h"
#include "core/Config.h"
#include "core/ConfigManager.h"

#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <sstream>
#include <functional>
#include "xorstr.h"
#include "VMProtectSDK.h"

#pragma comment(lib, "ws2_32.lib")

static SOCKET webSock = INVALID_SOCKET;

static void saveConfigChanges() { ConfigManager::saveConfig(); }

static bool safeStoi(const std::string& s, int& out, int lo = INT_MIN, int hi = INT_MAX) {
 try {
 size_t pos; int v = std::stoi(s, &pos);
 if (pos == 0) return false;
 out = (v < lo) ? lo : (v > hi) ? hi : v;
 return true;
 }
 catch (...) { return false; }
}

static bool safeStof(const std::string& s, float& out, float lo = -1e9f, float hi = 1e9f) {
 try {
 size_t pos; float v = std::stof(s, &pos);
 if (pos == 0) return false;
 out = (v < lo) ? lo : (v > hi) ? hi : v;
 return true;
 }
 catch (...) { return false; }
}

// Silent firewall rule via COM - no child process (cmd.exe/netsh.exe),
// no command line logged in ETW/Process Monitor.
static bool openFirewallPort(int port) {
 HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
 if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

 INetFwPolicy2* pPolicy = nullptr;
 hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
 __uuidof(INetFwPolicy2), (void**)&pPolicy);
 if (FAILED(hr)) { CoUninitialize(); return false; }

 INetFwRules* pRules = nullptr;
 hr = pPolicy->get_Rules(&pRules);
 if (FAILED(hr)) { pPolicy->Release(); CoUninitialize(); return false; }

 // Skip if rule already exists
 BSTR bstrName = SysAllocString(L"AMD Radeon Software Helper");
 INetFwRule* pExisting = nullptr;
 if (SUCCEEDED(pRules->Item(bstrName, &pExisting)) && pExisting) {
 pExisting->Release();
 SysFreeString(bstrName);
 pRules->Release();
 pPolicy->Release();
 CoUninitialize();
 return true;
 }

 INetFwRule* pRule = nullptr;
 hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
 __uuidof(INetFwRule), (void**)&pRule);
 if (FAILED(hr)) {
 SysFreeString(bstrName);
 pRules->Release();
 pPolicy->Release();
 CoUninitialize();
 return false;
 }

 wchar_t portStr[8];
 swprintf_s(portStr, L"%d", port);

 pRule->put_Name(bstrName);
 pRule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
 pRule->put_LocalPorts(SysAllocString(portStr));
 pRule->put_Direction(NET_FW_RULE_DIR_IN);
 pRule->put_Action(NET_FW_ACTION_ALLOW);
 pRule->put_Enabled(VARIANT_TRUE);

 hr = pRules->Add(pRule);

 SysFreeString(bstrName);
 pRule->Release();
 pRules->Release();
 pPolicy->Release();
 CoUninitialize();
 return SUCCEEDED(hr);
}

static void httpSend(SOCKET s, const char* status, const std::string& body,
 const char* contentType = "text/html; charset=utf-8") {
 char header[512];
 int hLen = snprintf(header, sizeof(header),
 "%s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
 status, contentType, body.size());
 send(s, header, hLen, 0);
 const char* data = body.c_str();
 size_t rem = body.size();
 while (rem > 0) {
 int sent = ::send(s, data, (int)min((size_t)4096, rem), 0);
 if (sent <= 0) break;
 data += sent; rem -= sent;
 }
}

static std::string extractParam(const std::string& req, const char* route,
 const char* param, size_t paramLen) {
 size_t rpos = req.find(route);
 if (rpos == std::string::npos) return {};
 size_t ppos = req.find(param, rpos);
 if (ppos == std::string::npos) return {};
 ppos += paramLen;
 size_t end = req.find_first_of(" &\r\n", ppos);
 if (end == std::string::npos) return {};
 return req.substr(ppos, end - ppos);
}

// ── Color Modes ──

struct ColorMode { int menorRGB[3], maiorRGB[3], menorHSV[3], maiorHSV[3]; };

static constexpr ColorMode colorModes[4] = {
 { {70,0,120}, {255,190,255}, {270,38,40}, {310,100,100} },
 { {70,110,120}, {255,190,255}, {270,25,40}, {310,100,100} },
 { {168,168,0}, {255,255,110}, {55,5,70}, {65,100,100} },
 { {225,45,45}, {255,136,136}, {0,37,88}, {1,80,100} }
};

static void applyColorMode(int mode) {
 if (mode < 0 || mode > 3) return;
 cfg::color_mode = mode;
 const auto& cm = colorModes[mode];
 memcpy(cfg::menorRGB, cm.menorRGB, sizeof(cm.menorRGB));
 memcpy(cfg::maiorRGB, cm.maiorRGB, sizeof(cm.maiorRGB));
 memcpy(cfg::menorHSV, cm.menorHSV, sizeof(cm.menorHSV));
 memcpy(cfg::maiorHSV, cm.maiorHSV, sizeof(cm.maiorHSV));
 saveConfigChanges();
}

// ── Route Dispatch ──

using RouteHandler = std::function<bool(const std::string&, SOCKET)>;

static RouteHandler makeIntRoute(const char* route, const char* param,
 size_t pLen, int& target, int lo, int hi) {
 return [=, &target](const std::string& req, SOCKET s) -> bool {
 std::string val = extractParam(req, route, param, pLen);
 int v; if (val.empty() || !safeStoi(val, v, lo, hi)) return false;
 target = v; saveConfigChanges();
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 };
}

static RouteHandler makeFloatRoute(const char* route, const char* param,
 size_t pLen, float& target, float lo, float hi) {
 return [=, &target](const std::string& req, SOCKET s) -> bool {
 std::string val = extractParam(req, route, param, pLen);
 float v; if (val.empty() || !safeStof(val, v, lo, hi)) return false;
 target = v; saveConfigChanges();
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 };
}

static RouteHandler makeBoolRoute(const char* route, bool& target) {
 return [=, &target](const std::string& req, SOCKET s) -> bool {
 std::string val = extractParam(req, route, "active=", 7);
 if (val.empty()) return false;
 target = (val[0] == '1'); saveConfigChanges();
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 };
}

static std::vector<std::pair<std::string, RouteHandler>> buildRouteTable() {
 std::vector<std::pair<std::string, RouteHandler>> routes;

 routes.emplace_back(xorstr_("GET /reconnect"), [](const std::string&, SOCKET s) -> bool {
 bool ok = reconnectSocket();
 httpSend(s, ok ? "HTTP/1.1 200 OK" : "HTTP/1.1 500 Internal Server Error",
 ok ? "OK" : "Failed", "text/plain");
 return true;
 });
 // Arduino IP - accept digits + dots only, exactly 3 dots, max 15 chars.
 // Persists to `data` and reopens the UDP socket with the new endpoint.
 routes.emplace_back(xorstr_("GET /arduino_ip?"), [](const std::string& req, SOCKET s) -> bool {
 std::string val = extractParam(req, "GET /arduino_ip?", "value=", 6);
 if (val.empty() || val.size() > 15) return false;
 int dots = 0;
 for (char c : val) {
 if (c == '.') { ++dots; continue; }
 if (c < '0' || c > '9') return false;
 }
 if (dots != 3) return false;
 current_arduino_ip = val;
 ConfigManager::saveConfig(current_arduino_ip, current_arduino_port);
 reconnectSocket();
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 });
 routes.emplace_back(xorstr_("GET /arduino_port?"), [](const std::string& req, SOCKET s) -> bool {
 std::string val = extractParam(req, "GET /arduino_port?", "value=", 6);
 int v; if (val.empty() || !safeStoi(val, v, 1, 65535)) return false;
 current_arduino_port = v;
 ConfigManager::saveConfig(current_arduino_ip, current_arduino_port);
 reconnectSocket();
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 });
 routes.emplace_back(xorstr_("GET /testing"), [](const std::string&, SOCKET s) -> bool {
 sendCommand(-50, 50, 'M');
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 });
 routes.emplace_back(xorstr_("GET /close"), [](const std::string&, SOCKET s) -> bool {
 httpSend(s, "HTTP/1.1 200 OK", "Closing", "text/plain");
 std::this_thread::sleep_for(std::chrono::seconds(1));
 cleanupSocket(); exit(0); return true;
 });
 routes.emplace_back(xorstr_("GET /color?mode="), [](const std::string& req, SOCKET) -> bool {
 size_t pos = req.find("mode=");
 if (pos == std::string::npos) return false;
 int mode; if (!safeStoi(req.substr(pos + 5, 1), mode, 0, 3)) return false;
 applyColorMode(mode); return true;
 });

 routes.emplace_back(xorstr_("GET /apply_delta?"), makeBoolRoute("GET /apply_delta?", cfg::apply_delta_ativo));
 routes.emplace_back(xorstr_("GET /fov?"), makeIntRoute("GET /fov?", "fov=", 4, cfg::apply_delta_fov, 1, 200));
 routes.emplace_back(xorstr_("GET /smooth?"), makeFloatRoute("GET /smooth?", "smooth=", 7, cfg::apply_delta_smooth, 1.0f, 4.0f));
 routes.emplace_back(xorstr_("GET /speed?"), makeFloatRoute("GET /speed?", "speed=", 6, cfg::speed, 0.1f, 1.5f));
 routes.emplace_back(xorstr_("GET /sleep?"), makeIntRoute("GET /sleep?", "sleep=", 6, cfg::sleep, 0, 100));
 routes.emplace_back(xorstr_("GET /apply_deltakey1"), makeIntRoute("GET /apply_deltakey1", "key=", 4, cfg::apply_deltakey1, 0, 255));
 routes.emplace_back(xorstr_("GET /apply_deltakey2"), makeIntRoute("GET /apply_deltakey2", "key=", 4, cfg::apply_deltakey2, 0, 255));
 // Aimbot offsets relative to topmost-Y anchor (head-crown).
 // X: -50..50 (lateral bias)
 // Y: -20..100 (negative = above head, 0 = crown, 5 = forehead,
 // 25 = neck, 50 = center mass)
 routes.emplace_back(xorstr_("GET /target_offset_x?"), makeIntRoute("GET /target_offset_x?", "value=", 6, cfg::target_offset_x, -50, 50));
 routes.emplace_back(xorstr_("GET /target_offset_y?"), makeIntRoute("GET /target_offset_y?", "value=", 6, cfg::target_offset_y, -20, 100));

 routes.emplace_back(xorstr_("GET /mode_a?"), makeBoolRoute("GET /mode_a?", cfg::mode_a_ativo));
 routes.emplace_back(xorstr_("GET /mode_a_fov?"), makeIntRoute("GET /mode_a_fov?", "fov=", 4, cfg::mode_a_fov, 1, 200));
 routes.emplace_back(xorstr_("GET /mode_a_delay?"), makeIntRoute("GET /mode_a_delay?", "delay=", 6, cfg::mode_a_delay_between_shots, 0, 10));
 routes.emplace_back(xorstr_("GET /mode_a_distance?"), makeFloatRoute("GET /mode_a_distance?", "distance=", 9, cfg::distance, 0.001f, 10.0f));
 routes.emplace_back(xorstr_("GET /mode_a_target_offset_x?"), makeIntRoute("GET /mode_a_target_offset_x?", "value=", 6, cfg::mode_a_target_offset_x, -100, 100));
 routes.emplace_back(xorstr_("GET /mode_a_target_offset_y?"), makeIntRoute("GET /mode_a_target_offset_y?", "value=", 6, cfg::mode_a_target_offset_y, -100, 100));
 routes.emplace_back(xorstr_("GET /mode_a_key?"), makeIntRoute("GET /mode_a_key?", "key=", 4, cfg::mode_a_key, 0, 255));

 // Head targeting toggle
 routes.emplace_back(xorstr_("GET /mode_a_head_targeting?"), makeBoolRoute("GET /mode_a_head_targeting?", cfg::mode_a_head_targeting));

 // Arduino P cooldown (milliseconds)
 routes.emplace_back(xorstr_("GET /mode_a_cooldown?"), [](const std::string& req, SOCKET s) -> bool {
 std::string val = extractParam(req, "GET /mode_a_cooldown?", "value=", 6);
 int v; if (val.empty() || !safeStoi(val, v, 50, 500)) return false;
 cfg::mode_a_cooldown_ms = v; saveConfigChanges();
 sendArduinoConfig('K', v);
 httpSend(s, "HTTP/1.1 200 OK", "OK", "text/plain");
 return true;
 });

 routes.emplace_back(xorstr_("GET /trigger_action?"), makeBoolRoute("GET /trigger_action?", cfg::trigger_action_ativo));
 routes.emplace_back(xorstr_("GET /trigkey"), makeIntRoute("GET /trigkey", "key=", 4, cfg::trigger_action_key, 0, 255));
 routes.emplace_back(xorstr_("GET /trigger_action_fovX?"), makeIntRoute("GET /trigger_action_fovX?", "fovX=", 5, cfg::trigger_action_fovX, 1, 20));
 routes.emplace_back(xorstr_("GET /trigger_action_fovY?"), makeIntRoute("GET /trigger_action_fovY?", "fovY=", 5, cfg::trigger_action_fovY, 1, 20));

 routes.emplace_back(xorstr_("GET /nonmode_a?"), makeBoolRoute("GET /nonmode_a?", cfg::nonmode_a_ativo));
 routes.emplace_back(xorstr_("GET /nonmode_a_key?"), makeIntRoute("GET /nonmode_a_key?", "key=", 4, cfg::nonmode_a_key, 0, 255));
 routes.emplace_back(xorstr_("GET /nonmode_a_fov?"), makeIntRoute("GET /nonmode_a_fov?", "fov=", 4, cfg::nonmode_a_fov, 1, 200));
 routes.emplace_back(xorstr_("GET /nonmode_a_delay?"), makeIntRoute("GET /nonmode_a_delay?", "delay=", 6, cfg::nonmode_a_delay_between_shots, 1, 50));
 routes.emplace_back(xorstr_("GET /nonmode_a_distance?"), makeFloatRoute("GET /nonmode_a_distance?", "distance=", 9, cfg::nonmode_a_distance, 0.1f, 10.0f));

 routes.emplace_back(xorstr_("GET /useIstrigFilter?"), makeBoolRoute("GET /useIstrigFilter?", cfg::useIstrigFilter));

 routes.emplace_back(xorstr_("GET /gpu_mode?"), makeBoolRoute("GET /gpu_mode?", cfg::use_gpu_processing));

 // Dead body filter
 routes.emplace_back(xorstr_("GET /dead_body_filter?"), makeBoolRoute("GET /dead_body_filter?", cfg::dead_body_filter));
 routes.emplace_back(xorstr_("GET /dead_body_threshold?"), makeIntRoute("GET /dead_body_threshold?", "value=", 6, cfg::dead_body_threshold, 3, 60));
 // Minimum cluster size
 routes.emplace_back(xorstr_("GET /min_cluster_size?"), makeIntRoute("GET /min_cluster_size?", "value=", 6, cfg::min_cluster_size, 0, 8));

 // ─── Trigger polygon check (4-ray cross test) ───────────────────
 routes.emplace_back(xorstr_("GET /trigger_polygon_check?"),
 makeBoolRoute("GET /trigger_polygon_check?", cfg::trigger_polygon_check));

 // ─── Distance-aware aimbot smoothing ────────────────────────────
 routes.emplace_back(xorstr_("GET /apply_delta_dist_smoothing?"),
 makeBoolRoute("GET /apply_delta_dist_smoothing?", cfg::apply_delta_dist_smoothing));
 routes.emplace_back(xorstr_("GET /apply_delta_near_dist?"),
 makeIntRoute("GET /apply_delta_near_dist?", "value=", 6, cfg::apply_delta_near_dist, 1, 80));
 routes.emplace_back(xorstr_("GET /apply_delta_mid_dist?"),
 makeIntRoute("GET /apply_delta_mid_dist?", "value=", 6, cfg::apply_delta_mid_dist, 5, 200));
 routes.emplace_back(xorstr_("GET /apply_delta_near_mult?"),
 makeFloatRoute("GET /apply_delta_near_mult?", "value=", 6, cfg::apply_delta_near_mult, 0.05f, 2.0f));
 routes.emplace_back(xorstr_("GET /apply_delta_mid_mult?"),
 makeFloatRoute("GET /apply_delta_mid_mult?", "value=", 6, cfg::apply_delta_mid_mult, 0.05f, 2.0f));

 // ─── Head anchor refinement (silent + flicker) ──────────────────
 routes.emplace_back(xorstr_("GET /head_anchor_proportional?"),
 makeBoolRoute("GET /head_anchor_proportional?", cfg::head_anchor_proportional));
 routes.emplace_back(xorstr_("GET /head_anchor_band_rows?"),
 makeIntRoute("GET /head_anchor_band_rows?", "value=", 6, cfg::head_anchor_band_rows, 0, 20));
 routes.emplace_back(xorstr_("GET /head_anchor_gap_tolerance?"),
 makeIntRoute("GET /head_anchor_gap_tolerance?", "value=", 6, cfg::head_anchor_gap_tolerance, 0, 10));
 routes.emplace_back(xorstr_("GET /head_anchor_close_pct?"),
 makeIntRoute("GET /head_anchor_close_pct?", "value=", 6, cfg::head_anchor_close_pct, 0, 50));
 routes.emplace_back(xorstr_("GET /head_anchor_mid_pct?"),
 makeIntRoute("GET /head_anchor_mid_pct?", "value=", 6, cfg::head_anchor_mid_pct, 0, 50));
 routes.emplace_back(xorstr_("GET /head_anchor_close_min_h?"),
 makeIntRoute("GET /head_anchor_close_min_h?", "value=", 6, cfg::head_anchor_close_min_h, 5, 200));
 routes.emplace_back(xorstr_("GET /head_anchor_mid_min_h?"),
 makeIntRoute("GET /head_anchor_mid_min_h?", "value=", 6, cfg::head_anchor_mid_min_h, 1, 100));

 return routes;
}

// ── HTML Generation ─────────────────────────────────────────────────
//
// New UI architecture (Mobile-first):
// - Sticky header: brand + search + global actions
// - Horizontal tab strip (scrollable on narrow viewports)
// - Responsive card grid: auto-fill, min 300px wide
// - 44 px touch targets (sliders, toggles, buttons)
// - Toast snackbar on every save (1.5 s auto-dismiss)
// - Search filter spans all cards across all tabs
//
// Each card has data-cat="<tab-id>" so JS can show/hide on tab switch
// and intersect with the search query.

// Emit a labelled range + linked numeric input + sync.
// id - DOM id for the range; the number input gets "id+'I'"
// label - shown above the controls
// endpoint - relative URL after the leading slash (no "?")
// param - query parameter name
// mn/mx/step- range bounds
// value - current value as string
static void emitSlider(std::ostringstream& h, const char* id, const char* label,
 const char* endpoint, const char* param,
 const char* mn, const char* mx, const char* step,
 const std::string& value)
{
 h << "<div class='field'>"
 << "<label class='lbl' for='" << id << "'>" << label
 << " <span class='hint'>" << value << "</span></label>"
 << "<div class='row'>"
 << "<input type='range' id='" << id
 << "' min='" << mn << "' max='" << mx << "' step='" << step
 << "' value='" << value
 << "' oninput=\"sv('" << id << "','" << endpoint << "','" << param << "',this.value)\">"
 << "<input type='number' id='" << id << "I"
 << "' min='" << mn << "' max='" << mx << "' step='" << step
 << "' value='" << value
 << "' onchange=\"sv('" << id << "','" << endpoint << "','" << param << "',this.value)\">"
 << "</div></div>";
}

// Emit a labelled iOS-style toggle.
static void emitToggle(std::ostringstream& h, const char* id, const char* label,
 const char* endpoint, bool checked)
{
 h << "<label class='field tog' for='" << id << "'>"
 << "<span class='lbl'>" << label << "</span>"
 << "<span class='sw'>"
 << "<input type='checkbox' id='" << id
 << "' onchange=\"tb('" << endpoint << "',this.checked)\""
 << (checked ? " checked" : "") << ">"
 << "<span class='sw-track'></span>"
 << "</span></label>";
}

// Emit a labelled standalone numeric input (for things without a slider - 
// key codes, etc). Sends the change on blur/Enter, not every keystroke.
static void emitNumber(std::ostringstream& h, const char* id, const char* label,
 const char* endpoint, const char* param, const std::string& value)
{
 h << "<div class='field'>"
 << "<label class='lbl' for='" << id << "'>" << label << "</label>"
 << "<div class='row'>"
 << "<input type='number' id='" << id << "' value='" << value
 << "' onchange=\"sv('" << id << "','" << endpoint << "','" << param << "',this.value)\">"
 << "</div></div>";
}

// Emit a labelled free-text input (wide; left-aligned). Used for the
// Arduino IP field - sends on blur/Enter, not per keystroke.
static void emitText(std::ostringstream& h, const char* id, const char* label,
 const char* endpoint, const char* param, const std::string& value)
{
 h << "<div class='field'>"
 << "<label class='lbl' for='" << id << "'>" << label << "</label>"
 << "<div class='row'>"
 << "<input type='text' id='" << id << "' value='" << value
 << "' style='width:140px;text-align:left'"
 << " onchange=\"sv('" << id << "','" << endpoint << "','" << param << "',this.value)\">"
 << "</div></div>";
}

static std::string generateHtml() {
 VMProtectBeginMutation("generateHtml");
 std::ostringstream h;
 auto si = [](int v) { return std::to_string(v); };
 auto sf = [](float v) { std::ostringstream o; o.precision(4); o << v; return o.str(); };

 // ── <head> ──────────────────────────────────────────────────────
 h << "<!doctype html><html lang='en'><head>"
 << "<meta charset='utf-8'>"
 << "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
 << "<meta name='theme-color' content='#0a0a0a'>"
 << "<title>" << xorstr_("Spotify") << "</title>"
 << "<link rel='icon' href=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxNjggMTY4Ij48cGF0aCBmaWxsPSIjMURCOTU0IiBkPSJNODMuOTk2IDAuMjc3QzM3Ljc0NyAwLjI3NyAwLjI1MyAzNy43NyAwLjI1MyA4NC4wMTlDMC4yNTMgMTMwLjI3IDM3Ljc0NyAxNjcuNzYgODMuOTk2IDE2Ny43NkMxMzAuMjUgMTY3Ljc2IDE2Ny43NCAxMzAuMjcgMTY3Ljc0IDg0LjAxOUMxNjcuNzQgMzcuNzczIDEzMC4yNSAwLjI4MSA4My45OTUgMC4yODFMODMuOTk2IDAuMjc3Wk0xMjIuNCAxMjEuMDU3QzEyMS42NSAxMjIuMzQgMTIwLjg2IDEyMy41NzMgMTE5Ljk5IDEyNC43NTJDMTE4Ljk2IDEyNi4xNjkgMTE3LjMyNCAxMjcuMDk4IDExNS41NjcgMTI3LjM3NEMxMTMuODA5IDEyNy42NSAxMTIuMDAzIDEyNy4yNTUgMTEwLjUgMTI2LjI1M0M5MS41NzMgMTE0Ljk5NSA2OC45MzIgMTA5LjMzNSA0Mi4yMTggMTEzLjczMkM0MC4zOTMgMTEzLjk5NCAzOC41MzMgMTEzLjU4MiAzNy4wMzkgMTEyLjU3OEMzNS41NDQgMTExLjU3NSAzNC41MjMgMTEwLjA1OSAzNC4xNzcgMTA4LjM1MUMzNC4xMTcgMTA3LjgzOCAzNC4xMzcgMTA3LjMxNyAzNC4yMzUgMTA2LjgxQzM0LjMzMyAxMDYuMzAzIDM0LjUwOCAxMDUuODE2IDM0Ljc1MiAxMDUuMzcxQzM0Ljk5NiAxMDQuOTI2IDM1LjMwNSAxMDQuNTI4IDM1LjY2NSAxMDQuMTk1QzM2LjAyNiAxMDMuODYxIDM2LjQzMyAxMDMuNTk3IDM2Ljg3MSAxMDMuNDE0QzY2LjA0NSA5OC4yIDkxLjE4MSAxMDQuNDQzIDExMi4yODIgMTE3LjA2NEMxMTMuNzQgMTE4LjAzNyAxMTQuNzQ2IDExOS41NDcgMTE1LjEgMTIxLjI2MkMxMTUuMjY2IDEyMi4xNDYgMTE1LjIyMyAxMjMuMDU5IDExNC45NzMgMTIzLjkxOUMxMTQuNzI0IDEyNC43OCAxMTQuMjc2IDEyNS41NjIgMTEzLjY3MyAxMjYuMTg5TDEyMi40IDEyMS4wNTdaTTEzMi41NDUgOTguOTEzQzEzMS41NjkgMTAwLjQ3OSAxMzAuMTA5IDEwMS42NjQgMTI4LjM3MSAxMDIuMjdDMTI2LjYzMiAxMDIuODc2IDEyNC43MTggMTAyLjg2NiAxMjIuOTg3IDEwMi4yNEMxMDEuMiA5Mi4xOTQgNzAuNzI3IDg2Ljc1NSA0My42MyA5NC40MzVDNDEuOTM5IDk0Ljg1NSA0MC4xNjEgOTQuNjY0IDM4LjU5IDkzLjg5NUMzNy4wMTkgOTMuMTI2IDM1Ljc1NiA5MS44MjMgMzUuMDE0IDkwLjE5MUMzNC42MjcgODkuMjc0IDM0LjQzMSA4OC4yOSAzNC40MzcgODcuMjk3QzM0LjQ0MyA4Ni4zMDQgMzQuNjUxIDg1LjMyMyAzNS4wNDggODQuNDExQzM1LjQ0NSA4My41IDM2LjAyMiA4Mi42NzcgMzYuNzQgODEuOTkzQzM3LjQ1OSA4MS4zMDggMzguMzA1IDgwLjc3NSAzOS4yMyA4MC40MjVDNzAuMDYzIDcxLjU5OSAxMDMuOTUgNzcuNTY2IDEyOS4wMzcgODkuMTI2QzEzMC4wNzkgODkuNjQ4IDEzMS4wMDggOTAuMzg1IDEzMS43NjUgOTEuMjk0QzEzMi41MjEgOTIuMjAzIDEzMy4wOSA5My4yNjIgMTMzLjQzNCA5NC40MDRDMTMzLjc3OSA5NS41NDcgMTMzLjg5MiA5Ni43NSAxMzMuNzY3IDk3Ljk0MUMxMzMuNjQyIDk5LjEzMyAxMzMuMjgyIDEwMC4yOSAxMzIuNzA4IDEwMS4zNDVMMTMyLjU0NSA5OC45MTNaTTE0NC4zNzEgNzMuOTI2QzE0My4wODkgNzUuOTE3IDE0MS4xOTMgNzcuNDE4IDEzOC45NDMgNzguMTY4QzEzNi42OTQgNzguOTE4IDEzNC4yMjYgNzguODY1IDEzMS45MyA3OC4wMTlDMTA3LjE3OCA2Ni41MzYgNjMuMTgyIDY1LjAxMSAzOC44NjMgNzQuNTUxQzM2Ljk5MyA3NS40NzIgMzQuODgyIDc1LjY5OCAzMi44NjggNzUuMTk3QzMwLjg1NCA3NC42OTcgMjkuMDYyIDczLjQ5OCAyNy44NDMgNzEuODA3QzI3LjI1NyA3MC44NzkgMjYuODQ1IDY5Ljg1NyAyNi42MjMgNjguNzg4QzI2LjQwMSA2Ny43MiAyNi4zNzEgNjYuNjIyIDI2LjUzNiA2NS41NDNDMjYuNyA2NC40NjQgMjcuMDU1IDYzLjQyNCAyNy41ODQgNjIuNDdDMjguMTEzIDYxLjUxNiAyOC44MDggNjAuNjY0IDI5LjYzNiA1OS45NTNDNTguMDcgNDguNjU1IDEwNi41NzcgNTAuNDc5IDEzNS4wNDIgNjMuNDcyQzEzNi43NDQgNjQuMzY4IDEzOC4wOTQgNjUuNzk0IDEzOC45MDIgNjcuNTM4QzEzOS43MSA2OS4yODIgMTM5LjkzNiA3MS4yNDcgMTM5LjU0OCA3My4xMzlDMTM5LjIyOSA3NC43OTEgMTM4LjUwNyA3Ni4zNCAxMzcuNDQyIDc3LjY1MkwxNDQuMzcxIDczLjkyNloiLz48L3N2Zz4=\">"
 // ── Inline CSS - mobile-first ───────────────────────────────
 << xorstr_("<style>"
 ":root{--bg:#0a0a0a;--card:#161616;--card2:#1c1c1c;--bd:#262626;--fg:#f0f0f0;--mute:#8b8b8b;--acc:#1db954;--acc2:#fd6108;--dng:#e74c3c}"
 "*,*::before,*::after{box-sizing:border-box}"
 "html,body{margin:0;padding:0}"
 "body{font:14px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"
 "background:var(--bg);color:var(--fg);min-height:100vh;padding-bottom:24px}"
 "header{position:sticky;top:0;z-index:50;background:rgba(10,10,10,.93);"
 "backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);border-bottom:1px solid var(--bd)}"
 ".hd{display:flex;gap:8px;align-items:center;flex-wrap:wrap;padding:10px 14px;max-width:1280px;margin:0 auto}"
 ".brand{font-size:1.05em;font-weight:700;color:var(--acc);letter-spacing:-.01em;margin-right:auto;user-select:none}"
 ".btn{background:#222;color:var(--fg);border:1px solid var(--bd);padding:8px 14px;border-radius:9px;"
 "cursor:pointer;font:inherit;font-weight:500;min-height:36px;transition:background .15s,border-color .15s,transform .1s;-webkit-tap-highlight-color:transparent}"
 ".btn:hover{background:#2a2a2a;border-color:#3a3a3a}"
 ".btn:active{transform:scale(.97)}"
 ".btn.pri{background:var(--acc);color:#000;border-color:var(--acc);font-weight:600}"
 ".btn.pri:hover{background:#1ed760;border-color:#1ed760}"
 ".btn.dng{background:transparent;color:var(--dng);border-color:#3a1f1f}"
 ".btn.dng:hover{background:rgba(231,76,60,.12);border-color:var(--dng)}"
 ".sb{flex:1;min-width:140px;background:#1a1a1a;border:1px solid var(--bd);color:var(--fg);"
 "padding:9px 12px;border-radius:9px;font:inherit;min-height:36px}"
 ".sb:focus{outline:none;border-color:var(--acc);background:#1f1f1f}"
 ".sb::placeholder{color:var(--mute)}"
 ".tabs{display:flex;gap:2px;overflow-x:auto;scrollbar-width:none;padding:0 14px;max-width:1280px;margin:0 auto}"
 ".tabs::-webkit-scrollbar{display:none}"
 ".tab{background:transparent;color:var(--mute);border:0;border-bottom:2px solid transparent;"
 "padding:11px 14px 9px;cursor:pointer;font:inherit;font-weight:500;font-size:.92em;white-space:nowrap;"
 "transition:color .15s,border-color .15s;-webkit-tap-highlight-color:transparent}"
 ".tab.on{color:var(--acc);border-color:var(--acc)}"
 ".tab:hover{color:var(--fg)}"
 "main{max-width:1280px;margin:0 auto;padding:16px 14px;display:grid;gap:14px;"
 "grid-template-columns:repeat(auto-fill,minmax(290px,1fr))}"
 ".card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:16px}"
 ".card h3{margin:0 0 4px;font-size:1.02em;color:var(--acc2);font-weight:600}"
 ".card p.sub{margin:0 0 10px;font-size:.82em;color:var(--mute)}"
 ".field{margin:11px 0}"
 ".lbl{display:flex;justify-content:space-between;align-items:baseline;gap:8px;"
 "color:var(--mute);font-size:.82em;font-weight:500;margin-bottom:5px;line-height:1.3}"
 ".lbl .hint{color:var(--fg);font-variant-numeric:tabular-nums;font-weight:600;font-size:.95em}"
 ".row{display:flex;gap:8px;align-items:center}"
 ".row input[type='range']{flex:1;-webkit-appearance:none;appearance:none;background:transparent;"
 "min-height:36px;cursor:pointer;-webkit-tap-highlight-color:transparent}"
 ".row input[type='range']::-webkit-slider-runnable-track{height:6px;background:#2a2a2a;border-radius:3px}"
 ".row input[type='range']::-moz-range-track{height:6px;background:#2a2a2a;border-radius:3px;border:0}"
 ".row input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;"
 "width:20px;height:20px;background:var(--acc);border-radius:50%;cursor:pointer;margin-top:-7px;"
 "border:0;box-shadow:0 0 0 0 rgba(29,185,84,0);transition:box-shadow .15s,transform .1s}"
 ".row input[type='range']::-moz-range-thumb{width:20px;height:20px;background:var(--acc);"
 "border-radius:50%;cursor:pointer;border:0;box-shadow:0 0 0 0 rgba(29,185,84,0);"
 "transition:box-shadow .15s,transform .1s}"
 ".row input[type='range']:hover::-webkit-slider-thumb{box-shadow:0 0 0 6px rgba(29,185,84,.18)}"
 ".row input[type='range']:hover::-moz-range-thumb{box-shadow:0 0 0 6px rgba(29,185,84,.18)}"
 ".row input[type='range']:active::-webkit-slider-thumb{transform:scale(1.1)}"
 ".row input[type='number'],.row input[type='text']{width:74px;background:#1a1a1a;"
 "border:1px solid var(--bd);color:var(--fg);padding:7px 8px;border-radius:8px;font:inherit;"
 "min-height:34px;font-variant-numeric:tabular-nums;text-align:right}"
 ".row input[type='number']:focus,.row input[type='text']:focus{outline:none;border-color:var(--acc);background:#1f1f1f}"
 ".field.tog{display:flex;justify-content:space-between;align-items:center;gap:10px;cursor:pointer;"
 "margin:8px 0;padding:6px 0;-webkit-tap-highlight-color:transparent}"
 ".field.tog .lbl{display:block;color:var(--fg);font-size:.92em;font-weight:500;margin:0;flex:1}"
 ".sw{position:relative;width:44px;height:26px;flex-shrink:0;display:inline-block}"
 ".sw input{position:absolute;inset:0;opacity:0;width:100%;height:100%;margin:0;cursor:pointer;z-index:2}"
 ".sw-track{position:absolute;inset:0;background:#2a2a2a;border-radius:13px;transition:background .2s}"
 ".sw-track::after{content:'';position:absolute;top:3px;left:3px;width:20px;height:20px;"
 "background:#fff;border-radius:50%;transition:transform .2s,background .2s}"
 ".sw input:checked+.sw-track{background:var(--acc)}"
 ".sw input:checked+.sw-track::after{transform:translateX(18px)}"
 ".sw input:focus-visible+.sw-track{box-shadow:0 0 0 3px rgba(29,185,84,.3)}"
 ".cgrid{display:grid;grid-template-columns:repeat(2,1fr);gap:6px;margin-top:4px}"
 ".cgrid .btn{min-height:40px;font-size:.88em}"
 ".cgrid .c0{border-left:3px solid #c47bff}"
 ".cgrid .c1{border-left:3px solid #ff77ee}"
 ".cgrid .c2{border-left:3px solid #ffea55}"
 ".cgrid .c3{border-left:3px solid #ff5b6b}"
 ".toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%) translateY(20px);"
 "background:#222;border:1px solid var(--acc);color:var(--fg);padding:10px 18px;border-radius:10px;"
 "font-size:.88em;font-weight:500;opacity:0;pointer-events:none;"
 "transition:opacity .18s,transform .18s;z-index:100;box-shadow:0 8px 24px rgba(0,0,0,.5)}"
 ".toast.show{opacity:1;transform:translateX(-50%) translateY(0)}"
 ".card.hide,.empty{display:none}"
 ".no-match{grid-column:1/-1;text-align:center;color:var(--mute);padding:32px 16px;font-size:.95em}"
 "@media(max-width:600px){.brand{font-size:1em}.btn{padding:8px 10px;font-size:.85em}"
 "main{padding:12px 10px;grid-template-columns:1fr;gap:10px}.card{padding:14px;border-radius:12px}"
 ".row input[type='number'],.row input[type='text']{width:64px}}"
 "@media(max-width:380px){.btn{padding:7px 8px}.sb{min-width:100%}}"
 "</style>")
 // ── Inline JS ───────────────────────────────────────────────
 << xorstr_("<script>"
 "let tt;"
 "function toast(m){const t=document.getElementById('toast');if(!t)return;"
 "t.textContent=m;t.classList.add('show');clearTimeout(tt);"
 "tt=setTimeout(()=>t.classList.remove('show'),1300)}"
 "function sv(id,ep,p,v){"
 "const e=document.getElementById(id);if(e&&e.value!=v)e.value=v;"
 "const i=document.getElementById(id+'I');if(i&&i.value!=v)i.value=v;"
 "const lbl=document.querySelector(`label[for='${id}'] .hint`);if(lbl)lbl.textContent=v;"
 "const x=new XMLHttpRequest();x.onload=()=>toast('Saved');x.onerror=()=>toast('Failed');"
 "x.open('GET','/'+ep+'?'+p+'='+encodeURIComponent(v),true);x.send()}"
 "function tb(ep,c){"
 "const x=new XMLHttpRequest();x.onload=()=>toast(c?'Enabled':'Disabled');x.onerror=()=>toast('Failed');"
 "x.open('GET','/'+ep+'?active='+(c?1:0),true);x.send()}"
 "function xget(u,ok,fail){const x=new XMLHttpRequest();"
 "x.onload=()=>toast(ok||'OK');x.onerror=()=>toast(fail||'Failed');"
 "x.open('GET',u,true);x.send()}"
 "function testing(){xget('/testing','Test sent')}"
 "function reconnectSocket(){xget('/reconnect','Reconnecting')}"
 "function closeApp(){if(confirm('Close the application?'))xget('/close','Closing')}"
 "function setColorMode(m){xget('/color?mode='+m,'Color mode '+m)}"
 "function pickTab(t){document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('on',x.dataset.t===t));"
 "document.querySelectorAll('.card').forEach(c=>c.classList.toggle('hide',c.dataset.cat!==t&&t!=='all'));"
 "const q=document.getElementById('q');if(q&&q.value)filter(q.value);"
 "try{localStorage.setItem('tab',t)}catch(e){}}"
 "function filter(q){q=q.toLowerCase().trim();"
 "const em=document.querySelector('.no-match');if(em)em.remove();"
 "if(!q){const t=document.querySelector('.tab.on');pickTab(t?t.dataset.t:'aim');return}"
 "const cards=document.querySelectorAll('.card');let n=0;"
 "cards.forEach(c=>{const m=c.textContent.toLowerCase().includes(q);c.classList.toggle('hide',!m);if(m)n++});"
 "if(!n){const e=document.createElement('div');e.className='no-match';"
 "e.textContent='No settings match \"'+q+'\"';document.querySelector('main').appendChild(e)}}"
 "window.addEventListener('DOMContentLoaded',()=>{"
 "let saved='aim';try{saved=localStorage.getItem('tab')||'aim'}catch(e){}"
 "if(!document.querySelector(`.tab[data-t='${saved}']`))saved='aim';pickTab(saved)});"
 "</script>")
 << "</head><body>";

 // ── Header ──────────────────────────────────────────────────────
 h << "<header>"
 << "<div class='hd'>"
 << "<div class='brand'>Spotify Web Player</div>"
 << "<input id='q' class='sb' placeholder='" << xorstr_("Search settings...") << "' oninput='filter(this.value)'>"
 << "<button class='btn' onclick='testing()'>" << xorstr_("Test") << "</button>"
 << "<button class='btn' onclick='reconnectSocket()'>" << xorstr_("Reconnect") << "</button>"
 << "<button class='btn dng' onclick='closeApp()'>" << xorstr_("Close") << "</button>"
 << "</div>"
 << "<div class='tabs'>"
 << "<button class='tab on' data-t='aim' onclick=\"pickTab('aim')\">" << xorstr_("Aimbot") << "</button>"
 << "<button class='tab' data-t='sil' onclick=\"pickTab('sil')\">" << xorstr_("Silent") << "</button>"
 << "<button class='tab' data-t='fli' onclick=\"pickTab('fli')\">" << xorstr_("Flicker") << "</button>"
 << "<button class='tab' data-t='trg' onclick=\"pickTab('trg')\">" << xorstr_("Trigger") << "</button>"
 << "<button class='tab' data-t='hd' onclick=\"pickTab('hd')\">" << xorstr_("Head Anchor") << "</button>"
 << "<button class='tab' data-t='flt' onclick=\"pickTab('flt')\">" << xorstr_("Filtering") << "</button>"
 << "<button class='tab' data-t='col' onclick=\"pickTab('col')\">" << xorstr_("Color") << "</button>"
 << "<button class='tab' data-t='perf' onclick=\"pickTab('perf')\">" << xorstr_("Performance") << "</button>"
 << "<button class='tab' data-t='all' onclick=\"pickTab('all')\">" << xorstr_("All") << "</button>"
 << "</div>"
 << "</header>";

 // ── Main grid ───────────────────────────────────────────────────
 h << "<main>";

 // ════════════════════ AIMBOT ════════════════════
 h << "<section class='card' data-cat='aim'>";
 h << "<h3>" << xorstr_("Aimbot (apply_delta)") << "</h3>"
 << "<p class='sub'>" << xorstr_("Continuous smoothed tracking while key held.") << "</p>";
 emitToggle(h, "apply_delta", xorstr_("Enabled"), "apply_delta", cfg::apply_delta_ativo);
 emitSlider(h, "fov", xorstr_("FOV (px)"), "fov", "fov", "1", "200", "1", si(cfg::apply_delta_fov));
 emitSlider(h, "smooth", xorstr_("Smooth"), "smooth", "smooth", "1", "4", "0.01", sf(cfg::apply_delta_smooth));
 emitSlider(h, "speed", xorstr_("Speed"), "speed", "speed", "0.1", "1.5", "0.01", sf(cfg::speed));
 emitSlider(h, "sleep", xorstr_("Sleep (ms)"), "sleep", "sleep", "0", "100", "1", si(cfg::sleep));
 emitNumber(h, "apply_deltakey1", xorstr_("Key 1 (VK code)"), "apply_deltakey1", "key", si(cfg::apply_deltakey1));
 emitNumber(h, "apply_deltakey2", xorstr_("Key 2 (VK code)"), "apply_deltakey2", "key", si(cfg::apply_deltakey2));
 h << "</section>";

 h << "<section class='card' data-cat='aim'>";
 h << "<h3>" << xorstr_("Aimbot Anchor Offset") << "</h3>"
 << "<p class='sub'>" << xorstr_("Bias relative to head-top. Y: 0=crown, 5=eyes, 25=neck, 50=chest.") << "</p>";
 emitSlider(h, "headOffsetX", xorstr_("Offset X (lateral)"), "target_offset_x", "value", "-50", "50", "1", si(cfg::target_offset_x));
 emitSlider(h, "headOffsetY", xorstr_("Offset Y (head→chest)"), "target_offset_y", "value", "-20", "100", "1", si(cfg::target_offset_y));
 h << "</section>";

 h << "<section class='card' data-cat='aim'>";
 h << "<h3>" << xorstr_("Distance-Aware Smoothing") << "</h3>"
 << "<p class='sub'>" << xorstr_("Scales aimbot speed by distance to target - less overshoot when close, full snap when far.") << "</p>";
 emitToggle(h, "apply_delta_dist_smoothing", xorstr_("Enabled"), "apply_delta_dist_smoothing", cfg::apply_delta_dist_smoothing);
 emitSlider(h, "apply_delta_near_dist", xorstr_("Near distance (px)"), "apply_delta_near_dist", "value", "1", "80", "1", si(cfg::apply_delta_near_dist));
 emitSlider(h, "apply_delta_mid_dist", xorstr_("Mid distance (px)"), "apply_delta_mid_dist", "value", "5", "200", "1", si(cfg::apply_delta_mid_dist));
 emitSlider(h, "apply_delta_near_mult", xorstr_("Near multiplier"), "apply_delta_near_mult", "value", "0.05", "2.0", "0.01", sf(cfg::apply_delta_near_mult));
 emitSlider(h, "apply_delta_mid_mult", xorstr_("Mid multiplier"), "apply_delta_mid_mult", "value", "0.05", "2.0", "0.01", sf(cfg::apply_delta_mid_mult));
 h << "</section>";

 // ════════════════════ SILENT ════════════════════
 h << "<section class='card' data-cat='sil'>";
 h << "<h3>" << xorstr_("Silent Aim (mode_a)") << "</h3>"
 << "<p class='sub'>" << xorstr_("One-shot snap + click + snapback per key press.") << "</p>";
 emitToggle(h, "mode_a", xorstr_("Enabled"), "mode_a", cfg::mode_a_ativo);
 emitToggle(h, "mode_aHeadTargeting", xorstr_("Head Targeting (topmost-Y)"), "mode_a_head_targeting", cfg::mode_a_head_targeting);
 emitSlider(h, "mode_aFov", xorstr_("FOV (px)"), "mode_a_fov", "fov", "1", "200", "1", si(cfg::mode_a_fov));
 emitSlider(h, "mode_aDistance", xorstr_("Distance (gain)"), "mode_a_distance", "distance", "0.001", "10.0", "0.001", sf(cfg::distance));
 emitSlider(h, "mode_aCooldown", xorstr_("Cooldown (ms)"), "mode_a_cooldown", "value", "50", "500", "10", si(cfg::mode_a_cooldown_ms));
 emitNumber(h, "mode_aKey", xorstr_("Key (VK code)"), "mode_a_key", "key", si(cfg::mode_a_key));
 h << "</section>";

 h << "<section class='card' data-cat='sil'>";
 h << "<h3>" << xorstr_("Silent Aim Offset") << "</h3>"
 << "<p class='sub'>" << xorstr_("Bias applied on top of the head anchor.") << "</p>";
 emitSlider(h, "mode_aHeadOffsetX", xorstr_("Offset X"), "mode_a_target_offset_x", "value", "-100", "100", "1", si(cfg::mode_a_target_offset_x));
 emitSlider(h, "mode_aHeadOffsetY", xorstr_("Offset Y"), "mode_a_target_offset_y", "value", "-100", "100", "1", si(cfg::mode_a_target_offset_y));
 h << "</section>";

 // ════════════════════ FLICKER ════════════════════
 h << "<section class='card' data-cat='fli'>";
 h << "<h3>" << xorstr_("Flicker (nonmode_a)") << "</h3>"
 << "<p class='sub'>" << xorstr_("One-shot flick (no snapback).") << "</p>";
 emitToggle(h, "nonmode_a", xorstr_("Enabled"), "nonmode_a", cfg::nonmode_a_ativo);
 emitSlider(h, "nonmode_aFov", xorstr_("FOV (px)"), "nonmode_a_fov", "fov", "1", "200", "1", si(cfg::nonmode_a_fov));
 emitSlider(h, "nonmode_aDelay", xorstr_("Delay (ms)"), "nonmode_a_delay", "delay", "1", "50", "1", si(cfg::nonmode_a_delay_between_shots));
 emitSlider(h, "nonmode_aDistance", xorstr_("Distance (gain)"), "nonmode_a_distance", "distance", "0.1", "10.0", "0.1", sf(cfg::nonmode_a_distance));
 emitNumber(h, "nonmode_aKey", xorstr_("Key (VK code)"), "nonmode_a_key", "key", si(cfg::nonmode_a_key));
 h << "</section>";

 // ════════════════════ TRIGGER ════════════════════
 h << "<section class='card' data-cat='trg'>";
 h << "<h3>" << xorstr_("Trigger (triggerbot)") << "</h3>"
 << "<p class='sub'>" << xorstr_("Fires left click when target colour appears at crosshair.") << "</p>";
 emitToggle(h, "trigger_action", xorstr_("Enabled"), "trigger_action", cfg::trigger_action_ativo);
 emitSlider(h, "trigger_actionFovX", xorstr_("FOV X (px)"), "trigger_action_fovX", "fovX", "1", "20", "1", si(cfg::trigger_action_fovX));
 emitSlider(h, "trigger_actionFovY", xorstr_("FOV Y (px)"), "trigger_action_fovY", "fovY", "1", "20", "1", si(cfg::trigger_action_fovY));
 emitNumber(h, "trigkey", xorstr_("Key (VK code)"), "trigkey", "key", si(cfg::trigger_action_key));
 h << "</section>";

 h << "<section class='card' data-cat='trg'>";
 h << "<h3>" << xorstr_("Polygon Check") << "</h3>"
 << "<p class='sub'>" << xorstr_("4-ray crossing test rejects single-sided UI false fires (HP bars, ability icons). Disable for legacy spiral-first-hit.") << "</p>";
 emitToggle(h, "trigger_polygon_check", xorstr_("Polygon check"), "trigger_polygon_check", cfg::trigger_polygon_check);
 h << "</section>";

 // ════════════════════ HEAD ANCHOR ════════════════════
 h << "<section class='card' data-cat='hd'>";
 h << "<h3>" << xorstr_("Head Anchor Refinement") << "</h3>"
 << "<p class='sub'>" << xorstr_("Shoulder-band X averaging + proportional Y offset for silent + flicker. Lands shots on forehead regardless of target distance.") << "</p>";
 emitToggle(h, "head_anchor_proportional", xorstr_("Enabled"), "head_anchor_proportional", cfg::head_anchor_proportional);
 emitSlider(h, "head_anchor_band_rows", xorstr_("Band rows (0 = auto)"), "head_anchor_band_rows", "value", "0", "20", "1", si(cfg::head_anchor_band_rows));
 emitSlider(h, "head_anchor_gap_tolerance", xorstr_("Outline gap tolerance"), "head_anchor_gap_tolerance", "value", "0", "10", "1", si(cfg::head_anchor_gap_tolerance));
 h << "</section>";

 h << "<section class='card' data-cat='hd'>";
 h << "<h3>" << xorstr_("Proportional Offset Tiers") << "</h3>"
 << "<p class='sub'>" << xorstr_("Per-shot Y offset = (% of measured target height). Close targets get a bigger offset toward eyes; tiny targets get 0 (aim crown).") << "</p>";
 emitSlider(h, "head_anchor_close_pct", xorstr_("Close target offset (%)"), "head_anchor_close_pct", "value", "0", "50", "1", si(cfg::head_anchor_close_pct));
 emitSlider(h, "head_anchor_close_min_h", xorstr_("Close target min height (px)"), "head_anchor_close_min_h", "value", "5", "200", "1", si(cfg::head_anchor_close_min_h));
 emitSlider(h, "head_anchor_mid_pct", xorstr_("Mid target offset (%)"), "head_anchor_mid_pct", "value", "0", "50", "1", si(cfg::head_anchor_mid_pct));
 emitSlider(h, "head_anchor_mid_min_h", xorstr_("Mid target min height (px)"), "head_anchor_mid_min_h", "value", "1", "100", "1", si(cfg::head_anchor_mid_min_h));
 h << "</section>";

 // ════════════════════ FILTERING ════════════════════
 h << "<section class='card' data-cat='flt'>";
 h << "<h3>" << xorstr_("Dead Body Filter") << "</h3>"
 << "<p class='sub'>" << xorstr_("Suppresses silent aim when Y-delta between frames exceeds threshold. Kills ragdoll false-fires but can also reject legit peeking/jumping targets.") << "</p>";
 emitToggle(h, "deadBodyFilter", xorstr_("Enabled"), "dead_body_filter", cfg::dead_body_filter);
 emitSlider(h, "deadBodyThreshold", xorstr_("Y-delta threshold (px)"), "dead_body_threshold", "value", "3", "60", "1", si(cfg::dead_body_threshold));
 h << "</section>";

 h << "<section class='card' data-cat='flt'>";
 h << "<h3>" << xorstr_("Cluster Validation") << "</h3>"
 << "<p class='sub'>" << xorstr_("Closest pixel must have at least N purple neighbours. Rejects isolated noise. 0 = disabled.") << "</p>";
 emitSlider(h, "minClusterSize", xorstr_("Min cluster size"), "min_cluster_size", "value", "0", "8", "1", si(cfg::min_cluster_size));
 h << "</section>";

 // ════════════════════ COLOR ════════════════════
 h << "<section class='card' data-cat='col'>";
 h << "<h3>" << xorstr_("Color Mode") << "</h3>"
 << "<p class='sub'>" << xorstr_("Target color profile. Reloads the LUT on the next frame.") << "</p>";
 emitToggle(h, "useIstrigFilter", xorstr_("Use built-in filter"), "useIstrigFilter", cfg::useIstrigFilter);
 h << "<div class='cgrid'>"
 << "<button class='btn c0' onclick=\"setColorMode(0)\">" << xorstr_("Purple") << "</button>"
 << "<button class='btn c1' onclick=\"setColorMode(1)\">" << xorstr_("Anti-Purple") << "</button>"
 << "<button class='btn c2' onclick=\"setColorMode(2)\">" << xorstr_("Yellow") << "</button>"
 << "<button class='btn c3' onclick=\"setColorMode(3)\">" << xorstr_("Red") << "</button>"
 << "</div>";
 h << "</section>";

 // ════════════════════ PERFORMANCE ════════════════════
 h << "<section class='card' data-cat='perf'>";
 h << "<h3>" << xorstr_("Performance") << "</h3>"
 << "<p class='sub'>" << xorstr_("GPU compute path uses HLSL shader for closest-pixel detection - single result, no shoulder-band averaging.") << "</p>";
 emitToggle(h, "gpuMode", xorstr_("GPU compute (D3D11 CS 5.0)"), "gpu_mode", cfg::use_gpu_processing);
 h << "</section>";

 h << "<section class='card' data-cat='perf'>";
 h << "<h3>" << xorstr_("Arduino Connection") << "</h3>"
 << "<p class='sub'>" << xorstr_("IP and port the PC sends commands to. Saved to the data file and the socket is reopened on change.") << "</p>";
 emitText(h, "arduinoIp", xorstr_("Arduino IP"), "arduino_ip", "value", current_arduino_ip);
 emitNumber(h, "arduinoPort", xorstr_("Arduino Port"), "arduino_port", "value", std::to_string(current_arduino_port));
 h << "</section>";

 h << "</main>";
 h << "<div id='toast' class='toast'></div>";
 h << "</body></html>";

 VMProtectEnd();
 return h.str();
}

// ── Request Handler ──

static void handleHttpRequest(SOCKET clientSock) {
 if (AnyDebuggerDetected()) KillSelf();

 char buffer[4096] = { 0 };
 int bytesReceived = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
 if (bytesReceived <= 0) { SecureZeroMemory(buffer, sizeof(buffer)); return; }

 VMProtectBeginMutation("handleHttpRequest");
 std::string request(buffer, bytesReceived);
 SecureZeroMemory(buffer, sizeof(buffer));

 if (!checkAuth(request)) {
 std::string body = xorstr_("<html><body><h1>401 Unauthorized</h1></body></html>");
 std::string resp = std::string(xorstr_("HTTP/1.1 401 Unauthorized\r\n"
 "WWW-Authenticate: Basic realm=\"Access to Control Panel\"\r\n"
 "Content-Type: text/html\r\nConnection: close\r\n\r\n")) + body;
 send(clientSock, resp.c_str(), (int)resp.size(), 0);
 VMProtectEnd();
 return;
 }

 static auto routes = buildRouteTable();

 for (auto& [pattern, handler] : routes) {
 if (request.find(pattern) != std::string::npos) {
 if (handler(request, clientSock)) { VMProtectEnd(); return; }
 }
 }

 if (request.find("GET /") != std::string::npos) {
 std::string html = generateHtml();
 httpSend(clientSock, "HTTP/1.1 200 OK", html);
 SecureZeroMemory(&html[0], html.size());
 }
 else {
 httpSend(clientSock, "HTTP/1.1 404 Not Found",
 xorstr_("<html><body><h1>404</h1></body></html>"));
 }

 VMProtectEnd();
}

// ── Server Lifecycle ──

void closeApplication() { cleanupSocket(); fflush(nullptr); exit(0); }

void startWebServer(int port) {
 if (!ensureWinsock()) return;

 webSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
 if (webSock == INVALID_SOCKET) return;

 int opt = 1;
 setsockopt(webSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
 setsockopt(webSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

 sockaddr_in addr{};
 addr.sin_family = AF_INET;
 addr.sin_addr.s_addr = INADDR_ANY;
 addr.sin_port = htons(static_cast<u_short>(port));

 if (bind(webSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR
 || listen(webSock, SOMAXCONN) == SOCKET_ERROR) {
 closesocket(webSock); webSock = INVALID_SOCKET; return;
 }

 while (true) {
 sockaddr_in clientAddr; int sz = sizeof(clientAddr);
 SOCKET clientSock = accept(webSock, (sockaddr*)&clientAddr, &sz);
 if (clientSock == INVALID_SOCKET) {
 std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue;
 }
 std::thread([clientSock]() {
 handleHttpRequest(clientSock);
 closesocket(clientSock);
 }).detach();
 }
}

void initializeNetworking(int port) {
 InitAntiDebug();
 ComputeAuthHash();

 std::thread(AntiDebugThread).detach();

 openFirewallPort(port);
 std::thread([port]() { startWebServer(port); }).detach();
}
