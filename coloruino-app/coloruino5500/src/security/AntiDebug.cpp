#include "AntiDebug.h"

#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include "xorstr.h"

#pragma comment(lib, "ntdll.lib")

typedef NTSTATUS(WINAPI* NtQueryInfoProcess_t)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

static NtQueryInfoProcess_t _NtQIP = nullptr;

static constexpr PROCESSINFOCLASS kDebugPort = (PROCESSINFOCLASS)7;
static constexpr PROCESSINFOCLASS kDebugObjHandle = (PROCESSINFOCLASS)30;

std::atomic<uint64_t> g_AuthHash{ 0 };

void InitAntiDebug() {
 HMODULE ntdll = GetModuleHandleA(xorstr_("ntdll.dll"));
 if (!ntdll) return;
 _NtQIP = (NtQueryInfoProcess_t)GetProcAddress(ntdll, xorstr_("NtQueryInformationProcess"));
}

static bool Check_PEB() {
 return IsDebuggerPresent() != FALSE;
}

static bool Check_DebugPort() {
 if (!_NtQIP) return false;
 DWORD_PTR port = 0;
 NTSTATUS st = _NtQIP(GetCurrentProcess(), kDebugPort, &port, sizeof(port), nullptr);
 return NT_SUCCESS(st) && port != 0;
}

static bool Check_DebugObject() {
 if (!_NtQIP) return false;
 HANDLE hDbg = nullptr;
 NTSTATUS st = _NtQIP(GetCurrentProcess(), kDebugObjHandle, &hDbg, sizeof(hDbg), nullptr);
 return NT_SUCCESS(st) && hDbg != nullptr;
}

static bool Check_RemoteDebugger() {
 BOOL present = FALSE;
 CheckRemoteDebuggerPresent(GetCurrentProcess(), &present);
 return present != FALSE;
}

static bool Check_HWBreakpoints() {
 CONTEXT ctx{};
 ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
 if (!GetThreadContext(GetCurrentThread(), &ctx)) return false;
 return (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3);
}

static bool Check_HeapFlags() {
#ifdef _WIN64
 const BYTE* pPEB = (const BYTE*)__readgsqword(0x60);
 const BYTE* pHeap = *(const BYTE**)(pPEB + 0x30);
 uint32_t flags = *(const uint32_t*)(pHeap + 0x70);
 uint32_t forceFlags = *(const uint32_t*)(pHeap + 0x74);
#else
 const BYTE* pPEB = (const BYTE*)__readfsdword(0x30);
 const BYTE* pHeap = *(const BYTE**)(pPEB + 0x18);
 uint32_t flags = *(const uint32_t*)(pHeap + 0x40);
 uint32_t forceFlags = *(const uint32_t*)(pHeap + 0x44);
#endif
 return (flags & ~0x2) != 0 || forceFlags != 0;
}

static bool Check_Timing() {
 uint64_t t1 = __rdtsc();
 volatile int x = 0;
 for (int i = 0; i < 1000; ++i) x += i;
 uint64_t t2 = __rdtsc();
 return (t2 - t1) > 50'000'000ULL;
}

bool AnyDebuggerDetected() {
 return Check_PEB()
 || Check_DebugPort()
 || Check_DebugObject()
 || Check_RemoteDebugger()
 || Check_HWBreakpoints()
 || Check_HeapFlags()
 || Check_Timing();
}

[[noreturn]] void KillSelf() {
 SecureZeroMemory(&g_AuthHash, sizeof(g_AuthHash));
 TerminateProcess(GetCurrentProcess(), 0xDEAD);
 __assume(0);
}

void AntiDebugThread() {
 while (true) {
 if (AnyDebuggerDetected()) KillSelf();
 DWORD base = 200;
 LARGE_INTEGER t; QueryPerformanceCounter(&t);
 DWORD jitter = static_cast<DWORD>(t.QuadPart & 0x7F);
 Sleep(base + jitter);
 }
}
