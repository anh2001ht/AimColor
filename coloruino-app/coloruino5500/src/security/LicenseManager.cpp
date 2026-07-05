#include "LicenseManager.h"
#include "core/ConfigManager.h"
#include "core/Config.h"
#include "security/Auth.h"
#include "xorstr.h"
#include "VMProtectSDK.h"

#include <conio.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <Windows.h>
#include <intrin.h>
#include <iphlpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "rpcrt4.lib")

namespace {
 constexpr uint64_t kFnvOff = 14695981039346656037ULL;
 constexpr uint64_t kFnvPri = 1099511628211ULL;

 constexpr uint64_t ct_fnv1a(const char* s, size_t n) {
 uint64_t h = kFnvOff;
 for (size_t i = 0; i < n; ++i)
 h = (h ^ static_cast<uint8_t>(s[i])) * kFnvPri;
 return h;
 }

 // Pre-computed FNV-1a hash of the valid license key.
 // The actual key string is evaluated at compile time and never
 // appears in the compiled binary, only the 64-bit hash remains.
 //
 // PLACEHOLDER. Replace with your own 32-char lowercase hex license.
 // MUST match the literal in coloruino-loader/license.cpp.
 // Generate with rotate_secrets.py.
 static constexpr uint64_t VALID_KEY_HASH =
 ct_fnv1a("00000000000000000000000000000000", 32);
}

std::string LicenseManager::generateHWID() {
 VMProtectBeginUltra("gH");
 std::stringstream hwid;

 int cpuInfo[4] = { 0 };
 __cpuid(cpuInfo, 0);
 hwid << std::hex << cpuInfo[1] << cpuInfo[3] << cpuInfo[2];

 HKEY hKey;
 char buffer[256] = { 0 };
 DWORD bufferSize = sizeof(buffer);

 if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
 xorstr_("HARDWARE\\DESCRIPTION\\System\\BIOS"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
 RegQueryValueExA(hKey, xorstr_("SystemManufacturer"), NULL, NULL, (LPBYTE)buffer, &bufferSize);
 hwid << buffer;

 bufferSize = sizeof(buffer);
 memset(buffer, 0, sizeof(buffer));
 RegQueryValueExA(hKey, xorstr_("SystemProductName"), NULL, NULL, (LPBYTE)buffer, &bufferSize);
 hwid << buffer;

 RegCloseKey(hKey);
 }

 IP_ADAPTER_INFO adapterInfo[16];
 DWORD bufLen = sizeof(adapterInfo);
 DWORD status = GetAdaptersInfo(adapterInfo, &bufLen);

 if (status == ERROR_SUCCESS) {
 for (int i = 0; i < 6; i++) {
 hwid << std::hex << std::setfill('0') << std::setw(2)
 << (int)adapterInfo[0].Address[i];
 }
 }

 if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
 xorstr_("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
 bufferSize = sizeof(buffer);
 memset(buffer, 0, sizeof(buffer));
 RegQueryValueExA(hKey, xorstr_("InstallDate"), NULL, NULL, (LPBYTE)buffer, &bufferSize);
 hwid << buffer;
 RegCloseKey(hKey);
 }

 // PLACEHOLDER 24-hex (12-byte) salt. Replace via rotate_secrets.py.
 // MUST match: ConfigManager::encryptDecrypt key,
 // coloruino-loader/data_writer.cpp generate_app_hwid() salt + xor_encrypt() key,
 // and coloruino-loader/hwid.cpp kHwidSalt[].
 std::string salt = xorstr_("000000000000000000000000");
 hwid << salt;
 SecureZeroMemory(&salt[0], salt.size());
 SecureZeroMemory(buffer, sizeof(buffer));

 std::string result = hwid.str();
 VMProtectEnd();
 return result;
}

std::string LicenseManager::hashHWID(const std::string& hwid) {
 VMProtectBeginUltra("hH");
 // PLACEHOLDER 18-char ASCII alphanumeric hash key. Replace via rotate_secrets.py.
 // MUST match coloruino-loader/data_writer.cpp hash_app_hwid().
 std::string key = xorstr_("PLACEHOLDER_KEY_18");
 std::string result = hwid;
 const size_t keyLength = key.size();

 for (int round = 0; round < 3; round++) {
 for (size_t i = 0; i < result.length(); i++) {
 result[i] = result[i] ^ key[(i + round) % keyLength] ^ (round + 1);
 }
 }

 SecureZeroMemory(&key[0], key.size());

 std::stringstream hexStream;
 for (unsigned char c : result) {
 hexStream << std::hex << std::setfill('0') << std::setw(2) << (int)c;
 }

 std::string hex = hexStream.str();
 SecureZeroMemory(&result[0], result.size());
 VMProtectEnd();
 return hex;
}

bool LicenseManager::isLicensed() {
 VMProtectBeginUltra("iL");
 std::string ip;
 int port;
 if (!ConfigManager::loadIPAndPort(ip, port)) {
 VMProtectEnd();
 return false;
 }

 std::string currentHWID = hashHWID(generateHWID());
 std::string storedHWID = getLicenseFromConfig();

 bool result = !storedHWID.empty() && (currentHWID == storedHWID);

 SecureZeroMemory(&currentHWID[0], currentHWID.size());
 SecureZeroMemory(&storedHWID[0], storedHWID.size());

 VMProtectEnd();
 return result;
}

bool LicenseManager::validateAndBindLicense(const std::string& licenseKey) {
 VMProtectBeginUltra("vBL");

 // Compare via FNV-1a hash - the actual key never appears in the binary
 uint64_t inputHash = fnv1a64(licenseKey.c_str(), licenseKey.size());
 if (inputHash != VALID_KEY_HASH) {
 VMProtectEnd();
 return false;
 }

 std::string hwid = generateHWID();
 std::string hashedHWID = hashHWID(hwid);
 SecureZeroMemory(&hwid[0], hwid.size());

 bool result = saveLicenseToConfig(hashedHWID);
 SecureZeroMemory(&hashedHWID[0], hashedHWID.size());

 VMProtectEnd();
 return result;
}

std::string LicenseManager::getHiddenInput() {
 std::string input;
 char ch;

 HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
 DWORD mode;
 GetConsoleMode(hConsole, &mode);
 SetConsoleMode(hConsole, mode & ~ENABLE_ECHO_INPUT);

 while (true) {
 ch = _getch();
 if (ch == '\r' || ch == '\n') break;
 else if (ch == '\b' && !input.empty()) {
 input.pop_back();
 std::cout << "\b \b";
 }
 else if (ch >= 32 && ch <= 126) {
 input += ch;
 std::cout << '*';
 }
 }

 SetConsoleMode(hConsole, mode);
 std::cout << std::endl;
 return input;
}

bool LicenseManager::promptForLicense() {
 std::string inputKey;
 std::cout << xorstr_("=== LICENSE ACTIVATION ===") << std::endl;
 std::cout << xorstr_("This application requires a valid license key.") << std::endl;
 std::cout << xorstr_("Please enter your license key: ");

 inputKey = getHiddenInput();
 std::cout << std::endl;

 if (validateAndBindLicense(inputKey)) {
 SecureZeroMemory(&inputKey[0], inputKey.size());
 std::cout << xorstr_("License activated successfully!") << std::endl;
 std::cout << xorstr_("Application is now bound to this hardware.") << std::endl;
 return true;
 }
 else {
 SecureZeroMemory(&inputKey[0], inputKey.size());
 std::cout << xorstr_("Invalid license key!") << std::endl;
 std::cout << xorstr_("Application will now exit.") << std::endl;
 return false;
 }
}

bool LicenseManager::checkLicense() {
 VMProtectBeginUltra("cL");
 // Windows-subsystem build has no console for promptForLicense().
 // The loader is the single entry point for license entry; the app
 // performs defense-in-depth only - silent reject on mismatch.
 bool result = isLicensed();
 VMProtectEnd();
 return result;
}

std::string LicenseManager::getLicenseFromConfig() {
 VMProtectBeginMutation("gLFC");
 try {
 std::ifstream inFile(xorstr_("data"), std::ios::binary);
 if (!inFile.is_open()) {
 VMProtectEnd();
 return "";
 }

 std::stringstream buffer;
 buffer << inFile.rdbuf();
 inFile.close();

 std::string decryptedData = ConfigManager::encryptDecrypt(buffer.str());
 std::istringstream iss(decryptedData);
 std::string line;

 std::getline(iss, line); // IP
 std::getline(iss, line); // Port

 while (std::getline(iss, line)) {
 if (line.find(xorstr_("LICENSE_HWID=")) == 0) {
 std::string result = line.substr(13);
 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 VMProtectEnd();
 return result;
 }
 if (line == xorstr_("---CONFIG_START---")) break;
 }

 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 VMProtectEnd();
 return "";
 }
 catch (...) {
 VMProtectEnd();
 return "";
 }
}

bool LicenseManager::saveLicenseToConfig(const std::string& hashedHWID) {
 VMProtectBeginMutation("sLTC");
 try {
 std::string ip = xorstr_("192.168.1.10"); // placeholder default; user-configurable via WebUI
 int port = 5353;
 ConfigManager::loadIPAndPort(ip, port);

 std::stringstream seed;
 seed << ip << "\n" << port << "\n";
 seed << xorstr_("LICENSE_HWID=") << hashedHWID << "\n";
 seed << xorstr_("---CONFIG_START---") << "\n";

 std::string enc = ConfigManager::encryptDecrypt(seed.str());

 std::ofstream outFile(xorstr_("data"), std::ios::binary);
 if (!outFile.is_open()) {
 VMProtectEnd();
 return false;
 }
 outFile.write(enc.c_str(), enc.size());
 outFile.close();

 bool result = ConfigManager::saveConfig(ip, port);
 VMProtectEnd();
 return result;
 }
 catch (...) {
 VMProtectEnd();
 return false;
 }
}
