#include "data_writer.h"
#include "xorstr.h"

#include <Windows.h>
#include <iphlpapi.h>
#include <intrin.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#pragma comment(lib, "iphlpapi.lib")

namespace data_writer {

namespace {

// Read REG_SZ value into a std::string keeping the registry's raw byte
// length (matches coloruino-app LicenseManager.cpp's REG read pattern,
// which inserts the buffer into a stringstream as char*).
std::string read_reg_string(HKEY root, const char* path, const char* value) {
 HKEY h = nullptr;
 if (RegOpenKeyExA(root, path, 0, KEY_READ, &h) != ERROR_SUCCESS) return "";
 char buf[256] = {0};
 DWORD cb = sizeof(buf);
 DWORD type = 0;
 std::string result;
 if (RegQueryValueExA(h, value, nullptr, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS
 && cb > 0) {
 result.assign(buf, strnlen(buf, cb));
 }
 RegCloseKey(h);
 return result;
}

// Mirror of coloruino-app/.../LicenseManager::generateHWID.
// Any change here MUST be applied identically in the app - otherwise
// the LICENSE_HWID we write won't match what the app computes at
// startup, and the app will silently exit.
std::string generate_app_hwid() {
 std::stringstream hwid;

 int cpuInfo[4] = {0};
 __cpuid(cpuInfo, 0);
 hwid << std::hex << cpuInfo[1] << cpuInfo[3] << cpuInfo[2];

 hwid << read_reg_string(HKEY_LOCAL_MACHINE,
 xorstr_("HARDWARE\\DESCRIPTION\\System\\BIOS"),
 xorstr_("SystemManufacturer"));
 hwid << read_reg_string(HKEY_LOCAL_MACHINE,
 xorstr_("HARDWARE\\DESCRIPTION\\System\\BIOS"),
 xorstr_("SystemProductName"));

 IP_ADAPTER_INFO adapters[16];
 DWORD cb = sizeof(adapters);
 if (GetAdaptersInfo(adapters, &cb) == ERROR_SUCCESS) {
 for (int i = 0; i < 6; i++) {
 hwid << std::hex << std::setfill('0') << std::setw(2)
 << (int)adapters[0].Address[i];
 }
 }

 hwid << read_reg_string(HKEY_LOCAL_MACHINE,
 xorstr_("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
 xorstr_("InstallDate"));

 // PLACEHOLDER 24-hex / 12-byte HWID salt. MUST match the literal
 // in coloruino-app/.../LicenseManager.cpp::generateHWID.
 // Rotate via rotate_secrets.py.
 hwid << xorstr_("000000000000000000000000");

 return hwid.str();
}

// Mirror of coloruino-app/.../LicenseManager::hashHWID.
// 3-round byte-XOR with the LicenseHashKey, then hex-encode.
std::string hash_app_hwid(const std::string& hwid) {
 // PLACEHOLDER 18-char ASCII hash key. MUST match
 // coloruino-app/.../LicenseManager.cpp::hashHWID. Rotate via rotate_secrets.py.
 std::string key = xorstr_("PLACEHOLDER_KEY_18");
 std::string result = hwid;
 const size_t keyLen = key.size();

 for (int round = 0; round < 3; round++) {
 for (size_t i = 0; i < result.length(); i++) {
 result[i] = result[i] ^ key[(i + round) % keyLen] ^ (round + 1);
 }
 }

 SecureZeroMemory(&key[0], key.size());

 std::stringstream hex;
 for (unsigned char c : result) {
 hex << std::hex << std::setfill('0') << std::setw(2) << (int)c;
 }
 SecureZeroMemory(&result[0], result.size());
 return hex.str();
}

// Mirror of coloruino-app/.../ConfigManager::encryptDecrypt (symmetric XOR
// stream with the config XOR key). Same key string as the app - rotate
// together.
std::string xor_encrypt(const std::string& data) {
 // PLACEHOLDER 24-hex / 12-byte XOR key. MUST match
 // coloruino-app/.../ConfigManager.cpp::encryptDecrypt.
 std::string key = xorstr_("000000000000000000000000");
 std::string result = data;
 const size_t keyLen = key.size();
 for (size_t i = 0; i < result.length(); i++) {
 result[i] ^= key[i % keyLen];
 }
 SecureZeroMemory(&key[0], key.size());
 return result;
}

std::wstring data_file_path() {
 wchar_t exePath[MAX_PATH] = {0};
 DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
 if (len == 0 || len >= MAX_PATH) return xorstr_(L"data");
 for (DWORD i = len; i > 0; --i) {
 if (exePath[i - 1] == L'\\') {
 exePath[i] = L'\0';
 break;
 }
 }
 std::wstring path = exePath;
 path += xorstr_(L"data");
 return path;
}

} // namespace

bool data_exists() {
 std::wstring path = data_file_path();
 DWORD attrs = GetFileAttributesW(path.c_str());
 return attrs != INVALID_FILE_ATTRIBUTES
 && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool write_data_file(const std::string& ip, int port) {
 std::string hwidHash = hash_app_hwid(generate_app_hwid());
 if (hwidHash.empty()) return false;

 std::stringstream seed;
 seed << ip << "\n";
 seed << port << "\n";
 seed << xorstr_("LICENSE_HWID=") << hwidHash << "\n";
 seed << xorstr_("---CONFIG_START---") << "\n";

 SecureZeroMemory(&hwidHash[0], hwidHash.size());

 std::string plaintext = seed.str();
 std::string encrypted = xor_encrypt(plaintext);
 SecureZeroMemory(&plaintext[0], plaintext.size());

 std::ofstream f(data_file_path(), std::ios::binary | std::ios::trunc);
 if (!f.is_open()) return false;
 f.write(encrypted.c_str(), encrypted.size());
 bool ok = f.good();
 f.close();

 SecureZeroMemory(&encrypted[0], encrypted.size());
 return ok;
}

bool ensure_data_file() {
 if (data_exists()) return true;
 return write_data_file();
}

} // namespace data_writer
