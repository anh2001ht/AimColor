#pragma once

#include <cstddef>
#include <cstdint>

namespace hwid {

constexpr size_t kHwidLen = 32; // SHA-256 output

// Computes a stable per-machine HWID digest into `out`.
// Components hashed: CPUID(0) + CPUID(1) + BIOS manufacturer/product +
// first adapter MAC + Windows InstallDate + hardcoded salt.
// Returns false on BCrypt failure.
bool compute_hwid(uint8_t out[kHwidLen]);

} // namespace hwid
