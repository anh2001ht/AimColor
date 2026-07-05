#include "Auth.h"
#include "AntiDebug.h"

#include <atomic>
#include <windows.h>
#include "xorstr.h"
#include "VMProtectSDK.h"

static constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

uint64_t fnv1a64(const char* data, size_t len) noexcept {
 uint64_t h = FNV_OFFSET;
 for (size_t i = 0; i < len; ++i)
 h = (h ^ static_cast<uint8_t>(data[i])) * FNV_PRIME;
 return h;
}

static const char kB64[] =
 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::string& in) {
 std::string out;
 out.reserve(((in.size() + 2) / 3) * 4);
 int val = 0, valb = -6;
 for (unsigned char c : in) {
 val = (val << 8) + c;
 valb += 8;
 while (valb >= 0) { out.push_back(kB64[(val >> valb) & 0x3F]); valb -= 6; }
 }
 if (valb > -6) out.push_back(kB64[((val << 8) >> (valb + 8)) & 0x3F]);
 while (out.size() % 4) out.push_back('=');
 return out;
}

static const int8_t kB64Lut[128] = {
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
 52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
 -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
 15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
 -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
 41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
};

std::string Base64Decode(const std::string& in) {
 std::string out; out.reserve(in.size() * 3 / 4);
 int val = 0, valb = -8;
 for (unsigned char c : in) {
 if (c == '=' || c >= 128 || kB64Lut[c] < 0) continue;
 val = (val << 6) + kB64Lut[c];
 valb += 6;
 if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
 }
 return out;
}

void ComputeAuthHash() {
 VMProtectBeginMutation("ComputeAuthHash");

 // PLACEHOLDER WebUI HTTP Basic-auth credentials.
 // Generate with rotate_secrets.py and paste here. The xorstr macro
 // keeps them out of the .rdata section as plaintext.
 std::string user = xorstr_("placeholder_user");
 std::string pass = xorstr_("PlaceholderPassword123!");

 std::string raw = user + ":" + pass;
 std::string b64 = Base64Encode(raw);
 std::string full = "Basic " + b64;

 g_AuthHash.store(fnv1a64(full.c_str(), full.size()), std::memory_order_release);

 SecureZeroMemory(&user[0], user.size());
 SecureZeroMemory(&pass[0], pass.size());
 SecureZeroMemory(&raw[0], raw.size());
 SecureZeroMemory(&b64[0], b64.size());
 SecureZeroMemory(&full[0], full.size());

 VMProtectEnd();
}

bool checkAuth(const std::string& request) {
 VMProtectBeginMutation("checkAuth");

 size_t pos = request.find(xorstr_("Authorization: "));
 if (pos == std::string::npos) { VMProtectEnd(); return false; }
 size_t end = request.find("\r\n", pos);
 if (end == std::string::npos) { VMProtectEnd(); return false; }

 std::string hdr = request.substr(pos + 15, end - pos - 15);

 uint64_t incoming = fnv1a64(hdr.c_str(), hdr.size());
 bool ok = (incoming == g_AuthHash.load(std::memory_order_acquire));

 SecureZeroMemory(&hdr[0], hdr.size());

 VMProtectEnd();
 return ok;
}
