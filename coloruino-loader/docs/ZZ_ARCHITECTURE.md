# Kiến trúc ProcessHollowing

> **Phần bổ sung sau Phase 6 vào flow bên dưới**:
> - **antidebug::install()** + **winapi::init()** chạy trước mọi thứ khác.
> - **license::load_from_auth_file()** thử decrypt `auth.dat`
> (resolve cạnh exe đang chạy qua `GetModuleFileNameW`); nếu miss,
> `license_dialog::prompt()` lấy key.
> - **License -> SHA256(license || kBuildSalt) -> AES-256-CBC decrypt**
> mảng `TabTip32_exe[]` nhúng (giờ là ciphertext, không còn XOR).
> - **data_writer::ensure_data_file()** ghi file `data` của app
> cạnh exe nếu thiếu - triển khai client single-binary, không cần
> `config_generator.exe` ngoài.
> - **Process hollowing** bọc trong **retry loop 8 lần** -
> một số victim ngẫu nhiên không hollow sạch được; retry hữu hạn xử lý failure mode.

## Injection Flow

```
+--------------+
| main() |
| |
| 1. Read embedded PE (TabTip32_exe[])
| 2. Detect arch (32/64) + subsystem (GUI/CUI)
| 3. FreeConsole() if GUI payload
| |
| 4. FindRandomTargetProcess()
| +-- CreateToolhelp32Snapshot
| +-- Filter: accessible + matching arch + subsystem
| +-- Random select (mt19937 + random_device)
| |
| 5. GetProcessInfo(targetPid) -> get executable path
| |
| 6. CreateProcessA(path, CREATE_SUSPENDED)
| +-- +CREATE_NO_WINDOW if GUI payload
| |
| 7. IsWow64Process -> determine 32/64 bit target
| |
| 8. Get PEB + ImageBase from suspended process
| +-- 32-bit: WOW64_CONTEXT -> CTX.Ebx + 0x8
| +-- 64-bit: CONTEXT -> CTX.Rdx + 0x10
| |
| 9. Select RunPE variant:
| +-- IsPE32 + !HasReloc -> RunPE32
| +-- IsPE32 + HasReloc -> RunPEReloc32
| +-- !IsPE32 + !HasReloc -> RunPE64
| +-- !IsPE32 + HasReloc -> RunPEReloc64
| |
| 10. If success: CleanProcess (close handles, keep running)
| If fail: CleanAndExitProcess (terminate + close)
|
+--------------+
```

## Chi tiết RunPE (biến thể không relocation)

```
RunPE64(PI, image):
 |
 +-- Parse DOS + NT headers from image
 |
 +-- VirtualAllocEx at preferred ImageBase
 | Size: SizeOfImage
 | Protection: PAGE_READWRITE (temporary)
 |
 +-- WriteProcessMemory: PE headers
 |
 +-- For each section:
 | WriteProcessMemory: section data at VirtualAddress
 |
 +-- ApplySectionProtections:
 | For each section:
 | VirtualProtectEx -> SectionToProtection(characteristics)
 | Headers -> PAGE_READONLY
 |
 +-- GetThreadContext(suspended thread)
 |
 +-- WriteProcessMemory: update PEB ImageBase
 | Address: CTX.Rdx + 0x10 (PEB->ImageBaseAddress)
 |
 +-- CTX.Rcx = AllocBase + AddressOfEntryPoint
 | SetThreadContext
 |
 +-- ResumeThread -> payload executes
```

## Chi tiết RunPE Relocation

Các bước bổ sung so với non-relocation:

```
RunPEReloc64(PI, image):
 |
 +-- VirtualAllocEx at NULL (any address)
 |
 +-- DeltaImageBase = AllocAddress - OriginalImageBase
 | Update ImageBase in NT header to AllocAddress
 |
 +-- Write headers + sections (same as RunPE64)
 |
 +-- Find relocation section:
 | DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]
 | Find section containing that RVA
 |
 +-- Process relocation blocks:
 | For each IMAGE_BASE_RELOCATION block:
 | For each IMAGE_RELOCATION_ENTRY:
 | Skip Type == 0 (padding)
 | Read DWORD64 at (AllocBase + VirtualAddress + Offset)
 | Add DeltaImageBase
 | Write back
 |
 +-- ApplySectionProtections (same)
 |
 +-- Update context + resume (same)
```

## Memory Protection Map

```
PE Section Characteristics -> Windows Protection
-----------------------------------------------------
EXECUTE | WRITE -> PAGE_EXECUTE_READWRITE
EXECUTE | READ -> PAGE_EXECUTE_READ
EXECUTE -> PAGE_EXECUTE
WRITE -> PAGE_READWRITE
READ -> PAGE_READONLY
(none) -> PAGE_NOACCESS
PE Headers (after write) -> PAGE_READONLY
```

Bố cục PE điển hình:

| Section | Characteristics | Applied Protection |
|---------|----------------|-------------------|
| .text | EXECUTE \| READ | PAGE_EXECUTE_READ |
| .rdata | READ | PAGE_READONLY |
| .data | READ \| WRITE | PAGE_READWRITE |
| .rsrc | READ | PAGE_READONLY |
| .reloc | READ | PAGE_READONLY |

## Tiêu chí chọn process

```
GetCompatibleProcesses(sourceSubsystem, sourceIs64Bit):

 For each process in snapshot:
 SKIP if PID == 0 (System Idle)
 SKIP if PID == 4 (System)
 SKIP if PID == GetCurrentProcessId()
 
 Must pass OpenProcess(QUERY_INFORMATION | VM_READ)
 
 Is64BitProcess check:
 GetNativeSystemInfo -> AMD64?
 IsWow64Process -> if WoW64, it's 32-bit on 64-bit OS
 
 Subsystem match:
 IMAGE_SUBSYSTEM_WINDOWS_GUI (2) -> GUI processes only
 IMAGE_SUBSYSTEM_WINDOWS_CUI (3) -> Console processes only
 -1 -> any subsystem (fallback)
 
 Architecture match:
 64-bit payload -> 64-bit target only
 32-bit payload -> 32-bit (WoW64) target only
```

## Tích hợp VMProtect

### Vị trí marker

```cpp
// Critical injection functions - Ultra (virtualization + mutation)
RunPE32() -> VMProtectBeginUltra("RP32")
RunPE64() -> VMProtectBeginUltra("RP64")
RunPEReloc32() -> VMProtectBeginUltra("RPR32")
RunPEReloc64() -> VMProtectBeginUltra("RPR64")
main() -> VMProtectBeginUltra("MainFunction")

// Selection logic - Mutation only (faster)
FindRandomTargetProcess() -> VMProtectBeginMutation("FRT")
```

### Vì sao không cần Runtime DLL

VMProtect cung cấp hai nhóm API:

1. **Compile-time markers** (`VMProtectBeginUltra`, `VMProtectBeginMutation`, `VMProtectEnd`) - được VMProtect packer xử lý và biến thành code bảo vệ. Không cần DLL.

2. **Runtime functions** (`VMProtectIsDebuggerPresent`, `VMProtectIsVirtualMachinePresent`) - cần `VMProtectSDK64.dll` khi runtime.

Project này chỉ dùng nhóm 1. SDK header cung cấp marker macro, và VMProtect xử lý chúng khi pack. Không có runtime dependency.

## Error Handling

| Failure Point | Behavior |
|---------------|----------|
| Invalid DOS/NT signature | Return -1 |
| No compatible processes found | Return -1 |
| GetProcessInfo fails | Return -1 |
| HeapAlloc fails | Return -1 |
| CreateProcessA fails | Free heap, return -1 |
| VirtualAllocEx fails | Return FALSE |
| WriteProcessMemory fails (headers) | Return FALSE |
| WriteProcessMemory fails (section) | Return FALSE |
| Get/SetThreadContext fails | Return FALSE |
| RunPE returns FALSE | TerminateProcess + close handles |
| RunPE returns TRUE | Close handles (process keeps running) |
