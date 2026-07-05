# coloruino-loader (AMDRSHelper.exe)

PE injection loader that embeds a payload executable as a byte array
and injects it into a randomly selected compatible host process via
process hollowing (RunPE).

> **See also**:
> [Top-level README](../README.md) - orientation across the four binaries.
> [USER_GUIDE](../USER_GUIDE.md) - non-technical setup walkthrough.
> [BUILD_GUIDE](../BUILD_GUIDE.md) - exhaustive build/sign/deploy pipeline.
> [ARCHITECTURE](../ARCHITECTURE.md) - system-wide technical overview.
> [SECURITY](../SECURITY.md) - threat model and detection vectors.

> **Phase 6 updates** (current state):
> - License acquisition: `auth.dat` is resolved next to the running exe
> (was: CWD-relative, which broke persistence across launch methods).
> - Process hollowing: up to 8-attempt retry loop, because
> `FindRandomTargetProcess` occasionally picks a target that can't be
> hollowed cleanly (privileged / job-restricted / EDR-watched).
> - **Single-binary client deployment**: `data_writer.cpp` produces the
> app's `data` file next to `AMDRSHelper.exe` on first-time license
> entry. `config_generator.exe` is no longer shipped to clients.
> HWID computation in `data_writer.cpp` MUST stay in sync with
> `coloruino-app/.../LicenseManager.cpp`.
> - Signed: Authenticode self-signed via `tools/signing/02_sign_binary.ps1`.

---

## How It Works

```
1. Read embedded PE from TabTip32_exe_bytes.h
2. Detect payload architecture (32/64-bit) and subsystem (GUI/CUI)
3. Enumerate running processes, filter by matching architecture + subsystem
4. Pick random compatible process
5. CreateProcess(target, SUSPENDED)
6. VirtualAllocEx in target's address space
7. Write PE headers + sections
8. Apply per-section memory protections
9. Fix base relocations (if needed)
10. Update PEB image base + thread context entry point
11. ResumeThread -> payload runs inside target process
```

---

## RunPE Variants

| Function | Arch | Relocation | Context |
|----------|------|------------|---------|
| `RunPE32()` | x86 (WoW64) | No (preferred base) | WOW64_CONTEXT, EBX/EAX |
| `RunPE64()` | x64 | No (preferred base) | CONTEXT, RDX/RCX |
| `RunPEReloc32()` | x86 (WoW64) | Yes (base relocation applied) | WOW64_CONTEXT, EBX/EAX |
| `RunPEReloc64()` | x64 | Yes (base relocation applied) | CONTEXT, RDX/RCX |

**Selection logic:**
```
if source is 32-bit:
 if has relocation table -> RunPEReloc32
 else -> RunPE32
if source is 64-bit:
 if has relocation table -> RunPEReloc64
 else -> RunPE64
```

---

## Per-Section Memory Protection

Instead of blanket `PAGE_EXECUTE_READWRITE` (major heuristic flag), each section gets the minimum protection its characteristics require:

| Section Characteristics | Protection |
|------------------------|------------|
| EXECUTE + WRITE | PAGE_EXECUTE_READWRITE |
| EXECUTE + READ | PAGE_EXECUTE_READ |
| EXECUTE only | PAGE_EXECUTE |
| WRITE | PAGE_READWRITE |
| READ only | PAGE_READONLY |
| None | PAGE_NOACCESS |

PE headers get `PAGE_READONLY` after write.

All sections are initially written with `PAGE_READWRITE`, then `VirtualProtectEx` applies final protections after all writes complete.

---

## Target Process Selection

`FindRandomTargetProcess()` enumerates all processes and filters:

1. Skip PID 0 (System Idle), PID 4 (System), and own PID
2. Must be accessible (`OpenProcess` with `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`)
3. Architecture must match payload (64-bit payload -> 64-bit target)
4. Subsystem must match (GUI payload -> GUI target process)
5. If no subsystem match found, falls back to any-subsystem with matching architecture

Random selection uses `std::random_device` + `std::mt19937` for proper entropy.

---

## 32-bit Address Safety

`RunPEReloc32()` includes an explicit check: if `VirtualAllocEx` returns an address above 4GB (possible when targeting a 64-bit process with WoW64), the relocation delta would overflow 32-bit arithmetic and silently corrupt the image. The function detects this and returns FALSE instead.

---

## VMProtect Markers

Critical functions are wrapped with VMProtect SDK markers for code protection:

| Function | Marker | Level |
|----------|--------|-------|
| `RunPE32()` | `VMProtectBeginUltra("RP32")` | Ultra (virtualization + mutation) |
| `RunPE64()` | `VMProtectBeginUltra("RP64")` | Ultra |
| `RunPEReloc32()` | `VMProtectBeginUltra("RPR32")` | Ultra |
| `RunPEReloc64()` | `VMProtectBeginUltra("RPR64")` | Ultra |
| `FindRandomTargetProcess()` | `VMProtectBeginMutation("FRT")` | Mutation |
| `main()` | `VMProtectBeginUltra("MainFunction")` | Ultra |

These are compile-time markers only. No VMProtect DLL is needed at runtime.

---

## Build

### Requirements
- Visual Studio 2022+ (MSVC v143)
- Windows SDK 10.0+
- VMProtect SDK headers (`VMProtectSDK.h`)

### Linked Libraries
- `Psapi.lib` - Process enumeration

### Steps

1. Build the payload (coloruino) as x64 Release
2. Pack with VMProtect (Memory Protection **OFF**)
3. Use HxD to export the packed binary as a C byte array header:
 - File -> Export -> C Source
 - Save as `TabTip32_exe_bytes.h`
 - Array name must be `TabTip32_exe` with length `TabTip32_exe_len`
4. Place header in project directory
5. Build ProcessHollowing as x64 Release
6. Pack final executable with VMProtect (Memory Protection **ON**)

### Header Format Expected

```c
// TabTip32_exe_bytes.h
unsigned char TabTip32_exe[] = { 0x4D, 0x5A, ... };
unsigned int TabTip32_exe_len = sizeof(TabTip32_exe);
```

---

## VMProtect Settings

### For Coloruino (Step 2 - First Pack)

| Setting | Value | Reason |
|---------|-------|--------|
| Memory Protection | **OFF** | CRC checks read from disk; hollowed process has wrong file on disk |
| Import Protection | ON | Wraps imports for dynamic resolution |
| Resource Protection | ON | Standard |
| Packer | ON | Compression |
| Code Mutation | ON | For marked functions |

### For ProcessHollowing (Step 6 - Final Pack)

| Setting | Value | Reason |
|---------|-------|--------|
| Memory Protection | **ON** | Runs directly from disk, CRC checks valid |
| Import Protection | ON | Standard |
| Resource Protection | ON | Standard |
| Packer | ON | Compression |
| Code Mutation | ON | For marked functions |

---

## File Structure

```
Process Hollowing/
+-- Process_Hollowing.cpp # Main source (all RunPE variants + process selection)
+-- TabTip32_exe_bytes.h # Embedded payload (generated by HxD)
+-- VMProtectSDK.h # VMProtect SDK markers (compile-time only)
```
