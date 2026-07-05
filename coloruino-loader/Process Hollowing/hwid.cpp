#include "hwid.h"
#include "crypto.h"
#include "xorstr.h"

#include <Windows.h>
#include <iphlpapi.h>
#include <intrin.h>

#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace hwid {

namespace {

void append_bytes(std::vector<uint8_t>& v, const void* p, size_t n) {
 const uint8_t* b = static_cast<const uint8_t*>(p);
 v.insert(v.end(), b, b + n);
}

void append_reg_string(std::vector<uint8_t>& v, HKEY root,
 const char* path, const char* value) {
 HKEY h = nullptr;
 if (RegOpenKeyExA(root, path, 0, KEY_READ, &h) != ERROR_SUCCESS) return;
 BYTE buf[256] = {0};
 DWORD cb = sizeof(buf);
 DWORD type = 0;
 if (RegQueryValueExA(h, value, nullptr, &type, buf, &cb) == ERROR_SUCCESS
 && cb > 0 && cb <= sizeof(buf)) {
 append_bytes(v, buf, cb);
 }
 RegCloseKey(h);
}

void append_cpuid(std::vector<uint8_t>& v) {
 int info[4] = {0};
 __cpuid(info, 0);
 append_bytes(v, info, sizeof(info));
 __cpuid(info, 1);
 append_bytes(v, info, sizeof(info));
}

void append_first_mac(std::vector<uint8_t>& v) {
 IP_ADAPTER_INFO adapters[16];
 ULONG cb = sizeof(adapters);
 if (GetAdaptersInfo(adapters, &cb) == ERROR_SUCCESS) {
 append_bytes(v, adapters[0].Address, 6);
 }
}

// PLACEHOLDER salt for the loader's INTERNAL HWID (used to AES-encrypt
// auth.dat). This salt is INDEPENDENT of the app-side HWID salt in
// data_writer.cpp / LicenseManager.cpp (different format, different
// purpose). Rotate this byte array however you like it just needs
// to be stable across runs on the same machine.
constexpr uint8_t kHwidSalt[] = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00
};

} // namespace

bool compute_hwid(uint8_t out[kHwidLen]) {
 std::vector<uint8_t> buf;
 buf.reserve(512);

 append_cpuid(buf);
 append_reg_string(buf, HKEY_LOCAL_MACHINE,
 xorstr_("HARDWARE\\DESCRIPTION\\System\\BIOS"),
 xorstr_("SystemManufacturer"));
 append_reg_string(buf, HKEY_LOCAL_MACHINE,
 xorstr_("HARDWARE\\DESCRIPTION\\System\\BIOS"),
 xorstr_("SystemProductName"));
 append_first_mac(buf);
 append_reg_string(buf, HKEY_LOCAL_MACHINE,
 xorstr_("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
 xorstr_("InstallDate"));
 append_bytes(buf, kHwidSalt, sizeof(kHwidSalt));

 return crypto::sha256(buf.data(), buf.size(), out);
}

} // namespace hwid
