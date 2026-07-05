#include "license.h"
#include "crypto.h"
#include "hwid.h"
#include "xorstr.h"

#include <Windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace license {

namespace {

// auth.dat sits next to the loader binary, NOT in the current working
// directory. Launching from Explorer vs a shortcut vs the Run dialog
// gives different CWDs, which would split the persisted license across
// multiple files - re-prompting every time. Resolve the path via the
// exe's own location.
std::wstring auth_file_path() {
 wchar_t exePath[MAX_PATH] = {0};
 DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
 if (len == 0 || len >= MAX_PATH) {
 return xorstr_(L"auth.dat"); // fallback - bare relative
 }
 for (DWORD i = len; i > 0; --i) {
 if (exePath[i - 1] == L'\\') {
 exePath[i] = L'\0';
 break;
 }
 }
 std::wstring path = exePath;
 path += xorstr_(L"auth.dat");
 return path;
}

constexpr uint64_t kFnvOff = 14695981039346656037ULL;
constexpr uint64_t kFnvPri = 1099511628211ULL;

// Compile-time FNV-1a of the valid license key. At /O2 with constexpr
// folding, the source string never reaches .rdata - only the 8-byte
// hash does. Must match VALID_KEY_HASH in coloruino-app LicenseManager.
constexpr uint64_t ct_fnv1a() {
 // PLACEHOLDER. Replace with your own 32-char lowercase hex license.
 // Use rotate_secrets.py to generate one. This same string must be
 // pasted into LicenseManager.cpp AND supplied to encrypt_payload_aes.py.
 const char s[] = "00000000000000000000000000000000";
 uint64_t h = kFnvOff;
 for (size_t i = 0; i < sizeof(s) - 1; ++i)
 h = (h ^ static_cast<uint8_t>(s[i])) * kFnvPri;
 return h;
}
constexpr uint64_t kValidKeyHash = ct_fnv1a();

uint64_t fnv1a(const uint8_t* data, size_t len) {
 uint64_t h = kFnvOff;
 for (size_t i = 0; i < len; ++i)
 h = (h ^ data[i]) * kFnvPri;
 return h;
}

bool is_lower_hex(char c) {
 return (c >= '0' && c <= '9')
 || (c >= 'a' && c <= 'f');
}

// Derive Km = SHA256(HWID) - fixed for this machine.
bool derive_machine_key(uint8_t Km[crypto::kAesKeyLen]) {
 uint8_t hwidBytes[hwid::kHwidLen];
 if (!hwid::compute_hwid(hwidBytes)) return false;
 return crypto::sha256(hwidBytes, sizeof(hwidBytes), Km);
}

} // namespace

bool validate(const char* key, size_t len) {
 if (len != kLicenseLen) return false;
 for (size_t i = 0; i < len; ++i) {
 if (!is_lower_hex(key[i])) return false;
 }
 return fnv1a(reinterpret_cast<const uint8_t*>(key), len) == kValidKeyHash;
}

bool load_from_auth_file(uint8_t out[kLicenseLen]) {
 std::ifstream f(auth_file_path(), std::ios::binary);
 if (!f.is_open()) return false;

 uint8_t iv[crypto::kAesBlockLen];
 f.read(reinterpret_cast<char*>(iv), sizeof(iv));
 if (f.gcount() != static_cast<std::streamsize>(sizeof(iv))) return false;

 std::vector<uint8_t> ciphertext(
 (std::istreambuf_iterator<char>(f)),
 std::istreambuf_iterator<char>());
 f.close();

 if (ciphertext.empty() || ciphertext.size() > 256) return false;
 if (ciphertext.size() % crypto::kAesBlockLen != 0) return false;

 uint8_t Km[crypto::kAesKeyLen];
 if (!derive_machine_key(Km)) return false;

 std::vector<uint8_t> plaintext(ciphertext.size());
 size_t outLen = 0;
 if (!crypto::aes256_cbc_decrypt(Km, iv,
 ciphertext.data(), ciphertext.size(),
 plaintext.data(), plaintext.size(), &outLen)) {
 return false;
 }
 if (outLen != kLicenseLen) return false;

 if (!validate(reinterpret_cast<const char*>(plaintext.data()), outLen)) {
 return false;
 }

 memcpy(out, plaintext.data(), kLicenseLen);
 return true;
}

bool save_to_auth_file(const uint8_t licenseKey[kLicenseLen]) {
 uint8_t iv[crypto::kAesBlockLen];
 if (!crypto::secure_random(iv, sizeof(iv))) return false;

 uint8_t Km[crypto::kAesKeyLen];
 if (!derive_machine_key(Km)) return false;

 // CBC + PKCS#7: 32-byte plaintext → 48-byte ciphertext.
 uint8_t ciphertext[64];
 size_t cbCipher = 0;
 if (!crypto::aes256_cbc_encrypt(Km, iv,
 licenseKey, kLicenseLen,
 ciphertext, sizeof(ciphertext), &cbCipher)) {
 return false;
 }

 std::ofstream f(auth_file_path(), std::ios::binary | std::ios::trunc);
 if (!f.is_open()) return false;
 f.write(reinterpret_cast<const char*>(iv), sizeof(iv));
 f.write(reinterpret_cast<const char*>(ciphertext),
 static_cast<std::streamsize>(cbCipher));
 return f.good();
}

} // namespace license
