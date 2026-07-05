#pragma once

#include <cstddef>
#include <cstdint>

namespace crypto {

constexpr size_t kSha256Len = 32;
constexpr size_t kAesKeyLen = 32; // AES-256
constexpr size_t kAesBlockLen = 16; // AES block / IV size

// SHA-256 of `data` into `out` (must be kSha256Len bytes).
// Returns false on BCrypt failure.
bool sha256(const uint8_t* data, size_t len, uint8_t out[kSha256Len]);

// AES-256-CBC encrypt with PKCS#7 padding.
// `out` must have capacity >= input_len + kAesBlockLen.
// On success returns ciphertext length in *outLen.
bool aes256_cbc_encrypt(const uint8_t key[kAesKeyLen],
 const uint8_t iv[kAesBlockLen],
 const uint8_t* plaintext, size_t plaintext_len,
 uint8_t* out, size_t out_capacity,
 size_t* outLen);

// AES-256-CBC decrypt with PKCS#7 padding.
// `out` must have capacity >= ciphertext_len.
// On success returns plaintext length in *outLen.
bool aes256_cbc_decrypt(const uint8_t key[kAesKeyLen],
 const uint8_t iv[kAesBlockLen],
 const uint8_t* ciphertext, size_t ciphertext_len,
 uint8_t* out, size_t out_capacity,
 size_t* outLen);

// Fill `out[0..len)` with cryptographically-secure random bytes.
bool secure_random(uint8_t* out, size_t len);

} // namespace crypto
