#pragma once

#include <cstddef>
#include <cstdint>

namespace license {

constexpr size_t kLicenseLen = 32; // 32 lowercase hex characters

// Returns true if `key[0..len)` is a valid 32-char lowercase hex string
// AND its FNV-1a 64-bit hash matches the constant burned into the loader.
bool validate(const char* key, size_t len);

// Try to load the saved (HWID-encrypted) license from auth.dat in CWD.
// On success writes 32 license bytes to `out` (NOT null-terminated)
// and returns true. On any failure (missing file, decrypt fail, format
// fail, FNV mismatch) returns false.
bool load_from_auth_file(uint8_t out[kLicenseLen]);

// Encrypts `licenseKey` (32 bytes) with HWID-derived key and writes to
// auth.dat in CWD. Returns false on any crypto/IO failure.
bool save_to_auth_file(const uint8_t licenseKey[kLicenseLen]);

} // namespace license
