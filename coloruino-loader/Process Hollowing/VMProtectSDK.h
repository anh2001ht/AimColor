#pragma once

// Stub VMProtectSDK - the loader is NOT VMProtect-packed (Phase 4
// decision: homebrew obfuscation + dynamic imports only). All marker
// macros expand to no-ops so the existing VMProtectBeginUltra /
// VMProtectBeginMutation / VMProtectEnd calls in Process_Hollowing.cpp
// stay source-compatible without pulling in a third-party header.
//
// The inner coloruino-app payload still uses the real VMProtect SDK
// (coloruino-app/coloruino5500/vendor/VMProtectSDK.h) and is packed
// separately before being embedded into this loader.
//
// If you later choose to VMProtect-pack the loader, replace this file
// with the real SDK header from your VMProtect install.

#define VMProtectBeginVirtualization(name) ((void)0)
#define VMProtectBeginMutation(name) ((void)0)
#define VMProtectBeginUltra(name) ((void)0)
#define VMProtectBeginVirtualizationLockByKey(name) ((void)0)
#define VMProtectBeginUltraLockByKey(name) ((void)0)
#define VMProtectEnd() ((void)0)
