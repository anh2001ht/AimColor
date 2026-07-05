// TabTip32_exe_bytes.h
//
// Holds the AES-256-CBC ciphertext bytes of your packed pipanel.exe
// (the embedded payload that the loader decrypts and hollows into a
// random target process).
//
// In the public release this is a STUB. The loader will compile but
// the embedded payload is just zero bytes there is nothing real to
// decrypt. Process hollowing will fail fast at the IsValidPE check.
//
// To produce a working build:
//
// 1. Build coloruino-app Release|x64. Output: pipanel.exe.
// 2. VMProtect-pack pipanel.exe (optional but recommended).
// 3. Open pipanel.exe in HxD. Edit > Select All >
// Edit > Copy as > C > "Unsigned char array".
// Paste the result here REPLACING the placeholder array below.
// Keep the symbol names `TabTip32_exe` and `TabTip32_exe_len`.
// 4. From this directory run:
// python gen_build_secrets.py (creates build_secrets.h)
// python encrypt_payload_aes.py <license>
// This AES-encrypts the TabTip32_exe[] array in place.
// 5. Build coloruino-loader.

#pragma once

#include <cstdint>

// Stub placeholder. Replace with the HxD dump of your packed pipanel.exe.
static uint8_t TabTip32_exe[] = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const size_t TabTip32_exe_len = sizeof(TabTip32_exe);
