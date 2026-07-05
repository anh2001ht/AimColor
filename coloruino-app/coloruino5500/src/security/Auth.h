#pragma once

#include <string>
#include <cstdint>

void ComputeAuthHash();
bool checkAuth(const std::string& request);

uint64_t fnv1a64(const char* data, size_t len) noexcept;
std::string Base64Encode(const std::string& in);
std::string Base64Decode(const std::string& in);
