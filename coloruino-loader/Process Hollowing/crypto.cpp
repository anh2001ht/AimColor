#include "crypto.h"

#include <Windows.h>
#include <bcrypt.h>
#include <cstring>
#include <new>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace crypto {

namespace {

struct AlgHandle {
 BCRYPT_ALG_HANDLE h = nullptr;
 ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};

struct KeyHandle {
 BCRYPT_KEY_HANDLE h = nullptr;
 ~KeyHandle() { if (h) BCryptDestroyKey(h); }
};

struct HashHandle {
 BCRYPT_HASH_HANDLE h = nullptr;
 ~HashHandle() { if (h) BCryptDestroyHash(h); }
};

} // namespace

bool sha256(const uint8_t* data, size_t len, uint8_t out[kSha256Len]) {
 AlgHandle alg;
 if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(
 &alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
 return false;
 }

 DWORD cbObj = 0, cbResult = 0;
 if (!NT_SUCCESS(BCryptGetProperty(alg.h, BCRYPT_OBJECT_LENGTH,
 reinterpret_cast<PUCHAR>(&cbObj), sizeof(cbObj), &cbResult, 0))) {
 return false;
 }

 BYTE* hashObj = new (std::nothrow) BYTE[cbObj];
 if (!hashObj) return false;

 HashHandle hash;
 bool ok = false;
 if (NT_SUCCESS(BCryptCreateHash(alg.h, &hash.h, hashObj, cbObj,
 nullptr, 0, 0))) {
 if (NT_SUCCESS(BCryptHashData(hash.h,
 const_cast<PUCHAR>(data),
 static_cast<ULONG>(len), 0))
 && NT_SUCCESS(BCryptFinishHash(hash.h, out,
 static_cast<ULONG>(kSha256Len), 0))) {
 ok = true;
 }
 }
 delete[] hashObj;
 return ok;
}

namespace {

bool prepare_aes_key(AlgHandle& alg, KeyHandle& key,
 const uint8_t k[kAesKeyLen]) {
 if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(
 &alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
 return false;
 }
 if (!NT_SUCCESS(BCryptSetProperty(alg.h, BCRYPT_CHAINING_MODE,
 reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
 sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) {
 return false;
 }
 if (!NT_SUCCESS(BCryptGenerateSymmetricKey(
 alg.h, &key.h, nullptr, 0,
 const_cast<PUCHAR>(k),
 static_cast<ULONG>(kAesKeyLen), 0))) {
 return false;
 }
 return true;
}

} // namespace

bool aes256_cbc_encrypt(const uint8_t key[kAesKeyLen],
 const uint8_t iv[kAesBlockLen],
 const uint8_t* plaintext, size_t plaintext_len,
 uint8_t* out, size_t out_capacity,
 size_t* outLen) {
 AlgHandle alg;
 KeyHandle k;
 if (!prepare_aes_key(alg, k, key)) return false;

 // BCryptEncrypt modifies the IV buffer in place. Copy first.
 uint8_t ivCopy[kAesBlockLen];
 memcpy(ivCopy, iv, kAesBlockLen);

 ULONG cb = 0;
 NTSTATUS st = BCryptEncrypt(k.h,
 const_cast<PUCHAR>(plaintext),
 static_cast<ULONG>(plaintext_len),
 nullptr,
 ivCopy, kAesBlockLen,
 out, static_cast<ULONG>(out_capacity),
 &cb, BCRYPT_BLOCK_PADDING);
 if (!NT_SUCCESS(st)) return false;

 if (outLen) *outLen = cb;
 return true;
}

bool aes256_cbc_decrypt(const uint8_t key[kAesKeyLen],
 const uint8_t iv[kAesBlockLen],
 const uint8_t* ciphertext, size_t ciphertext_len,
 uint8_t* out, size_t out_capacity,
 size_t* outLen) {
 AlgHandle alg;
 KeyHandle k;
 if (!prepare_aes_key(alg, k, key)) return false;

 uint8_t ivCopy[kAesBlockLen];
 memcpy(ivCopy, iv, kAesBlockLen);

 ULONG cb = 0;
 NTSTATUS st = BCryptDecrypt(k.h,
 const_cast<PUCHAR>(ciphertext),
 static_cast<ULONG>(ciphertext_len),
 nullptr,
 ivCopy, kAesBlockLen,
 out, static_cast<ULONG>(out_capacity),
 &cb, BCRYPT_BLOCK_PADDING);
 if (!NT_SUCCESS(st)) return false;

 if (outLen) *outLen = cb;
 return true;
}

bool secure_random(uint8_t* out, size_t len) {
 return NT_SUCCESS(BCryptGenRandom(nullptr, out,
 static_cast<ULONG>(len),
 BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

} // namespace crypto
