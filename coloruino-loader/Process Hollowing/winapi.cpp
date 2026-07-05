// IMPORTANT: this file deliberately does NOT include winapi.h, so the
// redirection macros are inactive here. We need the raw WinAPI symbols
// to assign their addresses (via GetProcAddress) into our function
// pointers. Including winapi.h would turn `pCreateProcessA = ...` into
// `winapi::pCreateProcessA = ...` (fine) but would also rewrite any
// other reference, creating self-reference loops.

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include "xorstr.h"

namespace winapi {

decltype(&::CreateProcessA) pCreateProcessA = nullptr;
decltype(&::WriteProcessMemory) pWriteProcessMemory = nullptr;
decltype(&::ReadProcessMemory) pReadProcessMemory = nullptr;
decltype(&::VirtualAllocEx) pVirtualAllocEx = nullptr;
decltype(&::VirtualProtectEx) pVirtualProtectEx = nullptr;
decltype(&::VirtualFreeEx) pVirtualFreeEx = nullptr;
decltype(&::GetThreadContext) pGetThreadContext = nullptr;
decltype(&::SetThreadContext) pSetThreadContext = nullptr;
decltype(&::Wow64GetThreadContext) pWow64GetThreadContext = nullptr;
decltype(&::Wow64SetThreadContext) pWow64SetThreadContext = nullptr;
decltype(&::ResumeThread) pResumeThread = nullptr;
decltype(&::TerminateProcess) pTerminateProcess = nullptr;
decltype(&::OpenProcess) pOpenProcess = nullptr;
decltype(&::CreateToolhelp32Snapshot) pCreateToolhelp32Snapshot = nullptr;
decltype(&::Process32FirstW) pProcess32FirstW = nullptr;
decltype(&::Process32NextW) pProcess32NextW = nullptr;
decltype(&::EnumProcessModules) pEnumProcessModules = nullptr;
decltype(&::GetModuleFileNameExA) pGetModuleFileNameExA = nullptr;

bool init() {
 // kernel32 is always already mapped at process start, so GetModuleHandle
 // (no LoadLibrary, no reference-count change) is sufficient.
 HMODULE k32 = GetModuleHandleA(xorstr_("kernel32.dll"));
 if (!k32) return false;

 pCreateProcessA = (decltype(pCreateProcessA)) GetProcAddress(k32, xorstr_("CreateProcessA"));
 pWriteProcessMemory = (decltype(pWriteProcessMemory)) GetProcAddress(k32, xorstr_("WriteProcessMemory"));
 pReadProcessMemory = (decltype(pReadProcessMemory)) GetProcAddress(k32, xorstr_("ReadProcessMemory"));
 pVirtualAllocEx = (decltype(pVirtualAllocEx)) GetProcAddress(k32, xorstr_("VirtualAllocEx"));
 pVirtualProtectEx = (decltype(pVirtualProtectEx)) GetProcAddress(k32, xorstr_("VirtualProtectEx"));
 pVirtualFreeEx = (decltype(pVirtualFreeEx)) GetProcAddress(k32, xorstr_("VirtualFreeEx"));
 pGetThreadContext = (decltype(pGetThreadContext)) GetProcAddress(k32, xorstr_("GetThreadContext"));
 pSetThreadContext = (decltype(pSetThreadContext)) GetProcAddress(k32, xorstr_("SetThreadContext"));
 pWow64GetThreadContext = (decltype(pWow64GetThreadContext)) GetProcAddress(k32, xorstr_("Wow64GetThreadContext"));
 pWow64SetThreadContext = (decltype(pWow64SetThreadContext)) GetProcAddress(k32, xorstr_("Wow64SetThreadContext"));
 pResumeThread = (decltype(pResumeThread)) GetProcAddress(k32, xorstr_("ResumeThread"));
 pTerminateProcess = (decltype(pTerminateProcess)) GetProcAddress(k32, xorstr_("TerminateProcess"));
 pOpenProcess = (decltype(pOpenProcess)) GetProcAddress(k32, xorstr_("OpenProcess"));
 pCreateToolhelp32Snapshot = (decltype(pCreateToolhelp32Snapshot)) GetProcAddress(k32, xorstr_("CreateToolhelp32Snapshot"));
 pProcess32FirstW = (decltype(pProcess32FirstW)) GetProcAddress(k32, xorstr_("Process32FirstW"));
 pProcess32NextW = (decltype(pProcess32NextW)) GetProcAddress(k32, xorstr_("Process32NextW"));

 // EnumProcessModules / GetModuleFileNameExA live in psapi historically;
 // since Win7 there are K32-prefixed kernel32 aliases. Resolve via
 // kernel32 to avoid a second DLL handle reference.
 pEnumProcessModules = (decltype(pEnumProcessModules)) GetProcAddress(k32, xorstr_("K32EnumProcessModules"));
 pGetModuleFileNameExA = (decltype(pGetModuleFileNameExA)) GetProcAddress(k32, xorstr_("K32GetModuleFileNameExA"));

 return pCreateProcessA && pWriteProcessMemory && pReadProcessMemory
 && pVirtualAllocEx && pVirtualProtectEx && pVirtualFreeEx
 && pGetThreadContext && pSetThreadContext
 && pWow64GetThreadContext && pWow64SetThreadContext
 && pResumeThread && pTerminateProcess && pOpenProcess
 && pCreateToolhelp32Snapshot && pProcess32FirstW && pProcess32NextW
 && pEnumProcessModules && pGetModuleFileNameExA;
}

} // namespace winapi
