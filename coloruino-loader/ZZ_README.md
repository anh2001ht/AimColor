# coloruino-loader (AMDRSHelper.exe)

PE injection loader nhúng payload executable dưới dạng byte array
và inject nó vào một host process tương thích được chọn ngẫu nhiên bằng
process hollowing (RunPE).

> **Xem thêm**:
> [README cấp root](../ZZ_README.md) - định hướng bốn binary.
> [USER_GUIDE](../ZZ_USER_GUIDE.md) - hướng dẫn setup không kỹ thuật.
> [BUILD_GUIDE](../ZZ_BUILD_GUIDE.md) - pipeline build/sign/deploy đầy đủ.
> [ARCHITECTURE](../ZZ_ARCHITECTURE.md) - tổng quan kỹ thuật toàn hệ thống.
> [SECURITY](../ZZ_SECURITY.md) - threat model và detection vector.

> **Cập nhật Phase 6** (trạng thái hiện tại):
> - Lấy license: `auth.dat` được resolve cạnh exe đang chạy
> (trước đây: tương đối với CWD, làm hỏng persistence giữa các cách launch).
> - Process hollowing: retry loop tối đa 8 lần, vì
> `FindRandomTargetProcess` đôi khi chọn target không thể hollow sạch
> (privileged / job-restricted / EDR-watched).
> - **Triển khai client single-binary**: `data_writer.cpp` tạo file `data`
> của app cạnh `AMDRSHelper.exe` khi nhập license lần đầu. Không còn ship
> `config_generator.exe` cho client. HWID computation trong `data_writer.cpp`
> PHẢI đồng bộ với `coloruino-app/.../LicenseManager.cpp`.
> - Signed: Authenticode self-signed qua `tools/signing/02_sign_binary.ps1`.

---

## Cách hoạt động

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
| `RunPE32()` | x86 (WoW64) | Không (preferred base) | WOW64_CONTEXT, EBX/EAX |
| `RunPE64()` | x64 | Không (preferred base) | CONTEXT, RDX/RCX |
| `RunPEReloc32()` | x86 (WoW64) | Có (base relocation applied) | WOW64_CONTEXT, EBX/EAX |
| `RunPEReloc64()` | x64 | Có (base relocation applied) | CONTEXT, RDX/RCX |

**Logic chọn:**
```
if source is 32-bit:
 if has relocation table -> RunPEReloc32
 else -> RunPE32
if source is 64-bit:
 if has relocation table -> RunPEReloc64
 else -> RunPE64
```

---

## Memory Protection theo Section

Thay vì blanket `PAGE_EXECUTE_READWRITE` (heuristic flag lớn), mỗi section nhận
protection tối thiểu theo characteristics:

| Section Characteristics | Protection |
|------------------------|------------|
| EXECUTE + WRITE | PAGE_EXECUTE_READWRITE |
| EXECUTE + READ | PAGE_EXECUTE_READ |
| EXECUTE only | PAGE_EXECUTE |
| WRITE | PAGE_READWRITE |
| READ only | PAGE_READONLY |
| None | PAGE_NOACCESS |

PE headers được đặt `PAGE_READONLY` sau khi write.

Toàn bộ section ban đầu được write với `PAGE_READWRITE`, sau đó
`VirtualProtectEx` áp final protection khi mọi write hoàn tất.

---

## Chọn Target Process

`FindRandomTargetProcess()` enumerate mọi process và filter:

1. Bỏ PID 0 (System Idle), PID 4 (System), và PID của chính mình
2. Phải access được (`OpenProcess` với `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`)
3. Architecture phải khớp payload (payload 64-bit -> target 64-bit)
4. Subsystem phải khớp (payload GUI -> GUI target process)
5. Nếu không có subsystem match, fallback sang any-subsystem cùng architecture

Random selection dùng `std::random_device` + `std::mt19937` để có entropy hợp lý.

---

## An toàn địa chỉ 32-bit

`RunPEReloc32()` có check rõ ràng: nếu `VirtualAllocEx` trả địa chỉ trên 4GB
(có thể khi target process 64-bit với WoW64), relocation delta sẽ overflow
arithmetic 32-bit và âm thầm corrupt image. Hàm detect việc này và trả FALSE.

---

## VMProtect Markers

Các function quan trọng được bọc bằng marker VMProtect SDK để bảo vệ code:

| Function | Marker | Level |
|----------|--------|-------|
| `RunPE32()` | `VMProtectBeginUltra("RP32")` | Ultra (virtualization + mutation) |
| `RunPE64()` | `VMProtectBeginUltra("RP64")` | Ultra |
| `RunPEReloc32()` | `VMProtectBeginUltra("RPR32")` | Ultra |
| `RunPEReloc64()` | `VMProtectBeginUltra("RPR64")` | Ultra |
| `FindRandomTargetProcess()` | `VMProtectBeginMutation("FRT")` | Mutation |
| `main()` | `VMProtectBeginUltra("MainFunction")` | Ultra |

Đây chỉ là compile-time marker. Không cần DLL VMProtect khi runtime.

---

## Build

### Requirements
- Visual Studio 2022+ (MSVC v143)
- Windows SDK 10.0+
- VMProtect SDK headers (`VMProtectSDK.h`)

### Linked Libraries
- `Psapi.lib` - Process enumeration

### Steps

1. Build payload (coloruino) dạng x64 Release
2. Pack bằng VMProtect (Memory Protection **OFF**)
3. Dùng HxD export binary đã pack thành C byte array header:
 - File -> Export -> C Source
 - Save as `TabTip32_exe_bytes.h`
 - Array name phải là `TabTip32_exe` với length `TabTip32_exe_len`
4. Đặt header trong project directory
5. Build ProcessHollowing dạng x64 Release
6. Pack executable cuối bằng VMProtect (Memory Protection **ON**)

### Header Format Expected

```c
// TabTip32_exe_bytes.h
unsigned char TabTip32_exe[] = { 0x4D, 0x5A, ... };
unsigned int TabTip32_exe_len = sizeof(TabTip32_exe);
```

---

## VMProtect Settings

### Cho Coloruino (Step 2 - First Pack)

| Setting | Value | Lý do |
|---------|-------|-------|
| Memory Protection | **OFF** | CRC check đọc từ đĩa; hollowed process có file sai trên đĩa |
| Import Protection | ON | Bọc import để dynamic resolution |
| Resource Protection | ON | Chuẩn |
| Packer | ON | Compression |
| Code Mutation | ON | Cho function có marker |

### Cho ProcessHollowing (Step 6 - Final Pack)

| Setting | Value | Lý do |
|---------|-------|-------|
| Memory Protection | **ON** | Chạy trực tiếp từ đĩa, CRC check hợp lệ |
| Import Protection | ON | Chuẩn |
| Resource Protection | ON | Chuẩn |
| Packer | ON | Compression |
| Code Mutation | ON | Cho function có marker |

---

## File Structure

```
Process Hollowing/
+-- Process_Hollowing.cpp # Main source (all RunPE variants + process selection)
+-- TabTip32_exe_bytes.h # Embedded payload (generated by HxD)
+-- VMProtectSDK.h # VMProtect SDK markers (compile-time only)
```
