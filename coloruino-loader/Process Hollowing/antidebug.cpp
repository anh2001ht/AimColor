// Adapted from umium (Apache 2.0, hotline1337) - slimmed for the loader.

#include "antidebug.h"
#include "xorstr.h"

#include <Windows.h>
#include <winternl.h>
#include <Psapi.h>

#include <thread>
#include <chrono>

#pragma comment(lib, "Psapi.lib")

namespace antidebug {

namespace {

using NtQueryInformationProcess_t = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static constexpr ULONG kProcessDebugPort = 7;
static constexpr ULONG kProcessDebugObjectHandle = 30;

// Multi-path termination - every path tried; if a debugger neuters one,
// the next still fires. Lifted from umium::trigger.
[[noreturn]] void trigger() {
 using rtl_raise_status_t = void (*)(LONG);
 using nt_terminate_process_t = LONG (NTAPI*)(HANDLE, LONG);

 HMODULE ntdll = GetModuleHandleA(xorstr_("ntdll.dll"));
 auto p_rtl_raise_status =
 reinterpret_cast<rtl_raise_status_t>(GetProcAddress(ntdll, xorstr_("RtlRaiseStatus")));
 auto p_nt_terminate_process =
 reinterpret_cast<nt_terminate_process_t>(GetProcAddress(ntdll, xorstr_("NtTerminateProcess")));

 while (true) {
 *reinterpret_cast<uintptr_t*>(0xFFFFFFFFFFFFFFFFULL) = 0xFFFFFFFFFFFFFFFFULL;
 if (p_rtl_raise_status) p_rtl_raise_status(static_cast<LONG>(0xDEAD));
 if (p_nt_terminate_process) p_nt_terminate_process(GetCurrentProcess(), static_cast<LONG>(0xDEAD));
 TerminateProcess(GetCurrentProcess(), 0xDEADu);
 ExitProcess(0xDEADu);
 FatalExit(0);
 DebugBreak();
 }
}

bool peb_being_debugged() {
#ifdef _WIN64
 auto peb = reinterpret_cast<PEB*>(__readgsqword(0x60));
#else
 auto peb = reinterpret_cast<PEB*>(__readfsdword(0x30));
#endif
 return peb && peb->BeingDebugged;
}

bool hardware_breakpoints_set() {
 CONTEXT ctx = {};
 ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
 if (!GetThreadContext(GetCurrentThread(), &ctx)) return false;
 return ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || ctx.Dr6 || ctx.Dr7;
}

bool nt_query_debug_port() {
 HMODULE ntdll = GetModuleHandleA(xorstr_("ntdll.dll"));
 auto p = reinterpret_cast<NtQueryInformationProcess_t>(
 GetProcAddress(ntdll, xorstr_("NtQueryInformationProcess")));
 if (!p) return false;
 DWORD_PTR port = 0;
 if (p(GetCurrentProcess(), kProcessDebugPort, &port, sizeof(port), nullptr) != 0)
 return false;
 return port != 0;
}

bool nt_query_debug_object_handle() {
 HMODULE ntdll = GetModuleHandleA(xorstr_("ntdll.dll"));
 auto p = reinterpret_cast<NtQueryInformationProcess_t>(
 GetProcAddress(ntdll, xorstr_("NtQueryInformationProcess")));
 if (!p) return false;
 HANDLE h = nullptr;
 if (p(GetCurrentProcess(), kProcessDebugObjectHandle, &h, sizeof(h), nullptr) != 0)
 return false;
 return h != nullptr;
}

bool check_remote_debugger() {
 BOOL present = FALSE;
 if (!CheckRemoteDebuggerPresent(GetCurrentProcess(), &present)) return false;
 return present != FALSE;
}

// PEB->NtGlobalFlag at +0xBC (x64) / +0x68 (x86). When a process is
// launched under a debugger, the loader OR's in three FLG_HEAP_* flags
// (0x10 | 0x20 | 0x40). Source: skywalker666 / apriorit.
bool nt_global_flag_set() {
#ifdef _WIN64
 auto peb_bytes = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
 DWORD flag = *reinterpret_cast<DWORD*>(peb_bytes + 0xBC);
#else
 auto peb_bytes = reinterpret_cast<uint8_t*>(__readfsdword(0x30));
 DWORD flag = *reinterpret_cast<DWORD*>(peb_bytes + 0x68);
#endif
 constexpr DWORD kDbgHeapFlags = 0x10 | 0x20 | 0x40;
 return (flag & kDbgHeapFlags) != 0;
}

// Process heap's Flags / ForceFlags. When run under a debugger, Flags
// loses HEAP_GROWABLE (0x2) and ForceFlags becomes non-zero. Offsets
// here target Vista+ (Win10 baseline). Source: skywalker666 / apriorit.
bool heap_flags_debug_marker() {
#ifdef _WIN64
 auto peb_bytes = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
 auto heap = *reinterpret_cast<uint8_t**>(peb_bytes + 0x30);
 if (!heap) return false;
 DWORD flags = *reinterpret_cast<DWORD*>(heap + 0x70);
 DWORD force_flags = *reinterpret_cast<DWORD*>(heap + 0x74);
#else
 auto peb_bytes = reinterpret_cast<uint8_t*>(__readfsdword(0x30));
 auto heap = *reinterpret_cast<uint8_t**>(peb_bytes + 0x18);
 if (!heap) return false;
 DWORD flags = *reinterpret_cast<DWORD*>(heap + 0x40);
 DWORD force_flags = *reinterpret_cast<DWORD*>(heap + 0x44);
#endif
 constexpr DWORD HEAP_GROWABLE_FLAG = 0x2;
 return (flags & ~HEAP_GROWABLE_FLAG) != 0 || force_flags != 0;
}

bool blacklisted_module_loaded() {
 // Conservative blacklist - only DLLs that, when loaded into our own
 // tiny loader process, are essentially unambiguous reverse-engineering
 // tooling. Anything Windows or AV-related (dbghelp, sbiedll, avghook,
 // snxhk, etc.) is excluded to avoid false-positive process kills on
 // ordinary user machines.
 static const char* const blacklist[] = {
 xorstr_("vehdebug-x86_64.dll"), // Cheat Engine VEH debugger
 xorstr_("HookLibraryx64.dll"), // Cheat Engine hook lib
 xorstr_("allochook-x86_64.dll"), // Cheat Engine alloc hook
 xorstr_("winhook-x86_64.dll"), // Cheat Engine win hook
 xorstr_("luaclient-x86_64.dll"), // Cheat Engine Lua
 xorstr_("api_log.dll"), // API Monitor
 xorstr_("wpespy.dll"), // WPE Pro
 };
 for (const char* name : blacklist) {
 if (GetModuleHandleA(name)) return true;
 }
 return false;
}

// Patches DbgBreakPoint and DbgUiRemoteBreakin → unconditional JMP ExitProcess.
// Debugger attach attempts redirect into ExitProcess instead of breaking in.
void patch_ntdll_debug_entries() {
 HMODULE ntdll = GetModuleHandleA(xorstr_("ntdll.dll"));
 if (!ntdll) return;

 FARPROC entries[] = {
 GetProcAddress(ntdll, xorstr_("DbgBreakPoint")),
 GetProcAddress(ntdll, xorstr_("DbgUiRemoteBreakin")),
 };

 // 0x48 0xB8 <imm64> 0xFF 0xE0 : mov rax, imm64 ; jmp rax (12 bytes)
 // Simpler: 0x68 <imm32> 0xC3 (push + ret) works for 32-bit targets;
 // for 64-bit we use mov rax / jmp rax.
 const uintptr_t target = reinterpret_cast<uintptr_t>(&ExitProcess);

 for (FARPROC entry : entries) {
 if (!entry) continue;
 DWORD oldProt = 0;
 if (!VirtualProtect(entry, 12, PAGE_EXECUTE_READWRITE, &oldProt)) continue;

 auto* p = reinterpret_cast<uint8_t*>(entry);
 p[0] = 0x48; // REX.W
 p[1] = 0xB8; // mov rax, imm64
 *reinterpret_cast<uint64_t*>(p + 2) = target;
 p[10] = 0xFF; // jmp rax
 p[11] = 0xE0;

 VirtualProtect(entry, 12, oldProt, &oldProt);
 }
}

bool any_tripwire() {
 return peb_being_debugged()
 || IsDebuggerPresent()
 || check_remote_debugger()
 || hardware_breakpoints_set()
 || nt_query_debug_port()
 || nt_query_debug_object_handle()
 || nt_global_flag_set()
 || heap_flags_debug_marker()
 || blacklisted_module_loaded();
}

void watchdog_loop() {
 while (true) {
 if (any_tripwire()) trigger();
 std::this_thread::sleep_for(std::chrono::seconds(1));
 }
}

// TLS callback - fires before main() (in DLL_PROCESS_ATTACH context).
// A reverser disassembling main() will never see this check; they'd
// have to know to look at IMAGE_DIRECTORY_ENTRY_TLS.
VOID NTAPI tls_dispatch(PVOID, DWORD reason, PVOID) {
 if (reason == DLL_PROCESS_ATTACH) {
 if (any_tripwire()) trigger();
 }
}

} // namespace

void install() {
 // Synchronous early check - must fire before license dialog or AES
 // decrypt run, so a debugger attached at process start can't reach
 // the license input.
 if (any_tripwire()) trigger();

 // Patch ntdll debug-attach entries to redirect to ExitProcess.
 patch_ntdll_debug_entries();

 // Background watchdog.
 std::thread(watchdog_loop).detach();
}

} // namespace antidebug

// ── TLS callback wiring ────────────────────────────────────────────────────
// File-scope so the linker can find these symbols via /INCLUDE. References
// the anonymous-namespace tls_dispatch by its in-TU visible address.

#pragma section(".CRT$XLY", long, read)

extern "C" __declspec(allocate(".CRT$XLY"))
PIMAGE_TLS_CALLBACK p_adb_tls_callback = antidebug::tls_dispatch;

// Force the linker to emit the TLS directory entry and keep our pointer.
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:p_adb_tls_callback")
