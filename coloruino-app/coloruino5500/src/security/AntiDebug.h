#pragma once

#include <cstdint>
#include <atomic>

void InitAntiDebug();
bool AnyDebuggerDetected();
[[noreturn]] void KillSelf();
void AntiDebugThread();

extern std::atomic<uint64_t> g_AuthHash;
