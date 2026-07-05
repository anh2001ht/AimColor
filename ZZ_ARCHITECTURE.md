# Coloruino - Kiến trúc hệ thống

Tài liệu kỹ thuật toàn hệ thống. Nội bộ từng component nằm trong README riêng dưới thư mục của component đó.

---

## Mô hình triển khai

### Mặc định - 1PC

Mọi thứ chạy trên MỘT PC. Valorant + Vanguard + `AMDRSHelper.exe` + Arduino đều nằm/cắm trên cùng máy. Cáp USB của Arduino là input chuột của OS; cáp Ethernet của Arduino nhận UDP từ ứng dụng cheat chạy trên cùng PC.

```
+------------------ YOUR PC -------------------+
|                                              |
|  AMDRSHelper.exe                             |
|    - prompts for license (first run)         |
|    - decrypts pipanel.exe in RAM             |
|    - hollows pipanel.exe into a random       |
|      legitimate target process               |
|         |                                    |
|         v                                    |
|  pipanel.exe (running inside e.g. dllhost)   |
|    - DXGI capture                            |
|    - color LUT classification                |
|    - aim decision                            |
|    - sendto() UDP :5353                      |
|         |                                    |
|         v                                    |
|     +--------+                               |
|     |  NIC   | --- Ethernet ---+             |
|     +--------+                  |            |
|                                 |            |
|  Valorant + Vanguard            |            |
|     ^                           |            |
|     | HID mouse via OS          |            |
|     |                           |            |
|     +-- USB ----- Arduino HID --+            |
|                       |                      |
+-----------------------|----------------------+
                        |
              +---------+---------+
              | Arduino Leonardo  |
              | + USB Host Shield |
              | + W5500           |
              +-------------------+
```

### Tùy chọn - 2PC + capture card

Ẩn hơn nhưng tốn phần cứng và dây hơn. `AMDRSHelper.exe` chạy trên CHEAT PC không bao giờ chạm Valorant. CHEAT PC nhìn màn hình GAME PC qua capture card. Arduino cắm USB vào GAME PC (xuất HID) và Ethernet vào CHEAT PC (nhận UDP).

```
+--- GAME PC -----------+      +--- CHEAT PC ----------+
| Valorant + Vanguard   |      | AMDRSHelper.exe       |
|        ^              |      |   - DXGI capture from |
|        | HID input    |      |     the capture-card  |
|        | via OS       |      |     virtual display   |
|        |              |      |        |              |
|   USB IN from         |      |        v              |
|   Arduino HID --------|      |   sendto() UDP        |
|                       |      |        |              |
|   HDMI OUT ---->------|--+   |        v              |
|   to capture card     |  |   |    +--------+         |
+-----------------------+  |   |    |  NIC   |---+     |
                           |   +----+--------+   |     |
                           |                     |     |
                           +-> capture card -----|--+  |
                               on CHEAT PC       |  |  |
                                                 |  |  |
                                          Ethernet  |  |
                                                 |  |  |
                                                 v  v  |
                                       +-------------------+
                                       | Arduino Leonardo  |
                                       | + USB Host Shield |
                                       | + W5500           |
                                       +-------------------+
                                                |
                                                | USB HID
                                                v
                                            (back into GAME PC)
```

Ở mode 2PC, GAME PC không có phần mềm coloruino. Process scan, module scan, liệt kê listening socket và behaviour monitoring của Vanguard trên GAME PC đều không thấy gì. CHEAT PC có toàn bộ cheat code, nhưng Vanguard không chạy ở đó.

Về code không có gì khác giữa 1PC và 2PC - cùng binary, cùng firmware, cùng wire protocol. Chỉ khác bố cục cáp vật lý. Tài liệu này nói về mặc định 1PC; 2PC giống vậy trừ góc nhìn Vanguard vào process cheat.

### PC build

PC build (Visual Studio, Python, VMProtect, HxD, Arduino IDE) nên TÁCH RIÊNG khỏi PC chơi bất kể bạn dùng mô hình triển khai nào. Tool dev, telemetry IDE, file PDB và footprint registry không nên nằm trên máy Vanguard scan.

---

## Bốn component

### coloruino-config-generator

Công cụ CLI độc lập. Trong setup release, phần này là TÙY CHỌN - `data_writer.cpp` của loader làm cùng việc ở first-run. Giữ trong repo như công cụ debug phía supplier để buộc tạo `data` mới mà không đi qua prompt license của loader.

Input:
- License key (`argv[1]`).
- HWID của máy hiện tại (tính tại chỗ).

Output:
- File `data`, mã hóa XOR bằng key XOR lúc build, chứa:
  - IP và port Arduino.
  - `LICENSE_HWID=<hash>`.
  - Dòng phân cách `---CONFIG_START---`.
  - Tất cả biến runtime `cfg::*` với default.

Validate license bằng constant FNV-1a riêng trước khi tạo output. License sai = không ghi `data`, thoát im lặng.

### coloruino-loader (AMDRSHelper.exe)

Binary nhìn thấy trên PC chơi. Chạy ở user level, không cần elevation.

Trách nhiệm:

1. Cài anti-debug (`antidebug::install()`).
2. Resolve WinAPI động (`winapi::init()`). Toàn bộ WinAPI dùng trong đường hollowing được resolve qua `GetProcAddress` với tên bị obfuscate bằng xorstr. Không bao giờ xuất hiện như static import.
3. Lấy license. Đọc `auth.dat` (mã hóa theo HWID), nếu không có thì hỏi qua native modal WinAPI dialog.
4. Giải mã payload. Dẫn xuất `key = SHA256(license || kBuildSalt)`, AES-256-CBC-decrypt mảng `TabTip32_exe[]` nhúng tại chỗ.
5. Chọn target. `FindRandomTargetProcess` chọn một user-level process cùng subsystem, cùng architecture từ `CreateToolhelp32Snapshot`.
6. Process hollowing. `CreateProcessA(target, CREATE_SUSPENDED | CREATE_NO_WINDOW)`. Duyệt header payload đã giải mã. `VirtualAllocEx` image base. `WriteProcessMemory` từng section. `VirtualProtectEx` đặt protection tối thiểu theo section. Patch relocation nếu có. Set RIP/EIP của thread context tới entry point payload. `ResumeThread`.
7. Retry loop. Tối đa 8 lần với các victim ngẫu nhiên khác nhau (một số process chọn ngẫu nhiên không hollow sạch được).
8. Dọn dẹp.

PE đã sanitize: post-build script xóa Rich header, timestamp, debug-data directory, randomize section names, tính lại PE checksum.

### coloruino-app (pipanel.exe)

Chạy hollow bên trong bất kỳ target nào loader chọn. Từ góc nhìn OS, nó trông như ví dụ `dllhost.exe` đang chạy bình thường.

Threads:

| Thread | Priority | Pacing | Vai trò |
|---|---|---|---|
| Capture | HIGHEST | DXGI 1ms | Grab frame + LUT classification + publish candidate từng mode |
| mode_a | HIGHEST | edge | Silent aim (snap một lần khi có key edge) |
| nonmode_a | HIGHEST | edge | Flicker (flick một lần, không snap-back) |
| WebServer | normal | accept loop | HTTP config UI trên :13548 |
| AntiDebug | normal | 1s tick | Detect debug liên tục |

Pipeline mỗi capture iteration:

```
AcquireNextFrame (DXGI 1ms timeout)
  |
  v
CopyResource into CPU staging texture
  |
  v
LUT.classify(rgb) -> boolean target/not-target
  |
  v
FindTargets(buffer, modes) -> 3 candidate coords
                              (aim / silent / flicker)
  |
  v
RefineHeadAnchor(silent, flicker)
  |
  v
Apply per-mode filters (cluster size, dead body)
  |
  v
Publish to globals: apply_delta_x/y,
                    mode_a_x/y,
                    nonmode_a_x/y
  |
  v
apply_delta()        - continuous tracking (writes UDP 'M' if key held)
Otrigger_action()    - triggerbot (writes UDP 'L' if conditions met)
```

Thread `mode_a` và `nonmode_a` theo dõi key edge tương ứng rồi gọi `SnapShoot_P` (UDP 'P') / `SnapShoot_F` (UDP 'F') khi phù hợp.

### coloruino-fw (Arduino Leonardo)

Arduino sketch single-threaded. Chạy khi cấp nguồn.

Chuỗi boot:

1. Khởi tạo HID Mouse interface (USB descriptor khai báo nó là danh tính chuột thật đã chọn theo AVR core đã patch).
2. Khởi tạo Ethernet shield với MAC OUI được rotate + IP cấu hình.
3. Bắt đầu listen UDP trên port 5353.

Main loop:

```
loop()
 +-- poll_udp_once()
 |     parsePacket -> read -> validate -> XOR decrypt -> CRC ->
 |     dispatch to exec_cmd()
 +-- Usb.Task()              real-mouse passthrough
 +-- poll_udp_once()         again, to catch packets that arrived
                              during Usb.Task's 100-500us blocking
```

Lệnh:

- `M dx dy` - movement chia sub-step với velocity curve và jitter 100-300us giữa step.
- `P dx dy` - silent aim: snap + click + snap-back. KHÔNG ĐỔI, deterministic.
- `F dx dy` - flicker: snap + click (không snap-back). KHÔNG ĐỔI, deterministic.
- `L`       - left click giữ ngẫu nhiên 20-67 ms.
- `K val`   - đặt `P_COOLDOWN` (cooldown gate silent-aim theo ms).

---

## Luồng dữ liệu end-to-end

Một hành động aim-assist điển hình từ pixel màn hình tới phát bắn:

```
T = 0.0 ms    Valorant renders frame N with an enemy outlined
              in purple at screen coord (1340, 520).

T = 0.5 ms    DXGI on the PC captures the desktop into a staging
              texture. (DXGI Desktop Duplication runs 1 frame
              behind the actual display. At 240 Hz this is
              ~4 ms; at 60 Hz, ~16 ms.)

T = 1.0 ms    CaptureLoop maps the staging texture, runs the
              16 MB RGB LUT classifier across the MAX-FOV pixel
              region. Finds the purple cluster, computes its
              head-anchor coord, publishes apply_delta_x/y =
              (target_x - screen_center, ...).

T = 1.05 ms   apply_delta() reads the new coords, computes a
              delta of (+30, -8) pixels with distance-smoothing
              applied, calls sendCommand(30, -8, 'M').

T = 1.10 ms   UDPClient packs the command into a 34-byte
              DNS-shaped packet, XORs the 10-byte payload with
              kProtoKey, sets the CRC, calls sendto() to the
              Arduino's IP on :5353.

T = 1.40 ms   Packet hits the Ethernet wire, arrives at the
              Arduino's W5500 buffer.

T = 1.70 ms   Arduino main loop polls UDP, finds the packet,
              validates the DNS-shape header, decrypts the XOR
              payload, decodes base32, dispatches to handle_M(30, -8).

T = 1.75 ms   handle_M splits (30, -8) into N sub-steps via
              velocity curve. For each sub-step:
                - emit Mouse.move(dx_i, dy_i)
                - random delay from the 100-300us jitter
              First Mouse.move queued to USB endpoint.

T = 1.90 ms   USB host on the PC polls the Leonardo's interrupt
              IN endpoint (bInterval=1ms). Reads HID report with
              first sub-step's dx/dy.

T = 2.00 ms   PC OS kernel processes the HID report, generates
              a WM_MOUSEMOVE / cursor delta visible to Valorant.

(repeats for each sub-step until full delta is applied)
```

Tổng latency end-to-end điển hình: 2-5 ms tùy refresh monitor và độ trễ DXGI.

---

## Chi tiết process hollowing

Loader dùng kỹ thuật RunPE suspended-process cổ điển với hardening hiện đại.

### Chọn target

```
GetCompatibleProcesses():
  CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)
  for each PROCESSENTRY32W:
    skip pid 0, pid 4, self
    OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ)
    Is64BitProcess() match
    EnumProcessModules() -> GetProcessSubsystem() match
    accessibility check
    push to candidate list

FindRandomTargetProcess():
  candidates = GetCompatibleProcesses(matchSubsystem, matchArch)
  if empty: candidates = GetCompatibleProcesses(any, matchArch)
  if empty: return 0
  std::mt19937(std::random_device()) -> uniform pick -> return PID
```

### Lõi hollowing (RunPE64 - 64-bit, không relocation)

```
1. Read payload's IMAGE_NT_HEADERS64.

2. CreateProcessA(targetPath,
                  CREATE_SUSPENDED [+ CREATE_NO_WINDOW for GUI])
   -> new suspended process, initial thread frozen at module entry.

3. VirtualAllocEx(target,
                  payload.ImageBase,
                  SizeOfImage,
                  MEM_COMMIT | RESERVE,
                  PAGE_READWRITE)

4. WriteProcessMemory(target, baseAddr, payload, SizeOfHeaders)

5. For each section:
     WriteProcessMemory(target,
                        baseAddr + s.VirtualAddress,
                        payload + s.PointerToRawData,
                        s.SizeOfRawData)

6. ApplySectionProtections: per-section VirtualProtectEx with
   SectionToProtection(s.Characteristics) - gives minimal RWE per
   section instead of blanket PAGE_EXECUTE_READWRITE. Headers
   locked to PAGE_READONLY.

7. GetThreadContext(thread)

8. WriteProcessMemory(target, CTX.Rdx + 0x10,
                      &payload.ImageBase, 8)
   <- updates the PEB's ImageBaseAddress so loader thinks the
      new image is "the" image

9. CTX.Rcx = baseAddr + AddressOfEntryPoint

10. SetThreadContext(thread)

11. ResumeThread -> execution starts at payload entry.
```

(`RunPEReloc64` tương tự nhưng áp relocation ở bước 6.)

WinAPI dynamic resolution nghĩa là `CreateProcessA`, `VirtualAllocEx`, `WriteProcessMemory`, `VirtualProtectEx`, `GetThreadContext`, `SetThreadContext`, `ResumeThread` và các hàm liên quan KHÔNG được import tĩnh. Static analysis trên `AMDRSHelper.exe` thấy không có import chữ ký hollowing.

### Retry loop

```
for attempt in 0..8:
  targetPid = FindRandomTargetProcess(subsystem, arch)
  if targetPid == 0: break
  try CreateProcess + RunPE...
  on success: break
  on failure: TerminateProcess(suspended), continue
```

Retry hữu hạn xử lý các failure mode khi pick ngẫu nhiên trúng process privileged / job-restricted / được EDR bảo vệ / tự thoát.

---

## Network protocol - hình dạng DNS

Packet UDP từ ứng dụng PC tới Arduino được tạo để nhìn giống DNS query nếu chỉ xem nhanh trên wire.

### Bố cục packet (tổng 34 byte)

```
offset  bytes  field           value
-------------------------------------------------------
0       2      transaction id  random per packet
2       2      flags           0x0100 (standard query, RD set)
4       2      qd_count        0x0001 (1 question)
6       2      an_count        0x0000
8       2      ns_count        0x0000
10      2      ar_count        0x0000
12      1      qname_len       16
13      16     qname           base32(XOR(payload, kProtoKey))
29      1      qname_term      0x00 (null root label)
30      2      qtype           0x0001 (A record)
32      2      qclass          0x0001 (IN class)
```

`qname` 16 byte mang base32 của command payload 10 byte đã XOR-encrypt + tail cố định/checksum 6 byte.

### Command payload (10 byte, sau XOR + base32 decode)

```
offset  bytes  field
---------------------------
0       1      magic     0xC0
1       1      cmd       'M' / 'P' / 'F' / 'L'
2       2      dx        int16 little-endian
4       2      dy        int16 little-endian
6       1      seq       sequence number (rolling)
7       2      reserved  0x0000
9       1      crc8      CRC-8 over bytes 0..8 (poly 0x07)
```

`L` (left click) bỏ qua dx/dy. Các lệnh khác dùng chúng làm movement delta.

### Vì sao dùng hình dạng này

mDNS dùng port 5353 và broadcast packet kiểu DNS trên mọi mạng local. Packet capture của protocol này trông không phân biệt được với chatter zeroconf thông thường nếu nhìn nhanh. Deep packet inspection sẽ thấy qname là chuỗi vô nghĩa (base32 của byte XOR-encrypted) thay vì hostname, nhưng thứ sniff sâu đến mức đó trên LAN riêng tư đã vượt qua phạm vi phòng thủ này rồi.

---

## Chuỗi dẫn xuất key

```
+----------------------+
| License (32 hex)     |
+----------+-----------+
           |
           |  used in 2 places:
           |
           |   (1) FNV-1a hash check (loader + app)
           |
           |   (2) SHA256(license || kBuildSalt)
           |       -> AES-256-CBC key for payload
           |          (loader runtime decrypt)
           v
+--------------------------------+
| kBuildSalt (32B random/build)  |  never leaves loader binary
+--------------------------------+
| kPayloadIV (16B random/build)  |  never leaves loader binary
+--------------------------------+
| kHwidSalt (12B compile-time)   |  matches config XOR key
+--------------------------------+


+--------------------------------------------+
|  Per-PC HWID = hash of:                    |
|    CPUID(0,1)                              |
|    HKLM\HARDWARE\...\SystemManufacturer    |
|    HKLM\HARDWARE\...\SystemProductName     |
|    first-MAC                               |
|    HKLM\...\InstallDate                    |
|    kHwidSalt                               |
+--------+-----------------------------------+
         |
         v
   +------------------------------------+
   | Km = SHA256(HWID)                  |
   | (used as AES key directly)         |
   +------+-----------------------------+
          |
          | used for:
          v
   +------------------------------------+
   | auth.dat = IV || AES-CBC(Km, lic)  |
   |   persists license across runs     |
   +------------------------------------+


+--------------------------------------------+
| data file (XOR-encrypted with config XOR   |
| key) stores:                               |
|   IP, port                                 |
|   LICENSE_HWID = HWID-hash-with-HASH_KEY   |
|   all cfg::* values                        |
+--------------------------------------------+
```

License là secret duy nhất người dùng cung cấp. Mọi thứ khác dẫn xuất từ nó + random lúc build + fingerprint theo máy. License không bao giờ tới app ở runtime; nó chỉ gate loader.

---

## Trạng thái trên đĩa

Các file tồn tại trên PC chơi:

```
AMDRSHelper.exe   signed, packed, sanitized - ~5-15 MB
data              XOR-encrypted, ~1.5 KB - written by the loader
                  on first license entry; rewritten by the app
                  when the user changes settings via WebUI
auth.dat          AES-encrypted (HWID-keyed), 48-64 bytes -
                  written by the loader on first license entry
```

Chỉ vậy. Không registry write. Không service cài đặt. Không scheduled task. Không autorun entry trừ khi bạn tự thêm shortcut vào Startup folder.

Firewall: một inbound rule tự tạo ở lần chạy đầu, tên "AMD Radeon Software Helper" cho TCP 13548 (port WebUI).

`config_generator.exe` không thuộc deployment release - module `data_writer` của loader tự ghi `data`.

---

## Trạng thái trong memory

Snapshot của một coloruino-app ĐANG CHẠY:

```
Process: <target-name>.exe   (whatever the loader picked,
                              e.g. dllhost.exe)
  Main thread: capture loop, HIGHEST priority
  Thread 2:    mode_a edge watcher, HIGHEST priority
  Thread 3:    nonmode_a edge watcher, HIGHEST priority
  Thread 4:    WebServer accept loop, NORMAL priority
  Thread 5:    AntiDebug watchdog, NORMAL priority

  Open handles:
    DXGI desktop duplication
    UDP socket (Arduino:5353 outbound)
    TCP socket (0.0.0.0:13548 inbound)
    HKEY for HWID computation (closed shortly after init)

  Memory:
    code/rdata/data - from the hollowed PE (RX / R / RW per section)
    16 MB color LUT - RW, populated at init
    capture staging buffer - varies with MAX-FOV

  Windows:
    none registered (no window class created at runtime)
```

Snapshot của loader (`AMDRSHelper.exe`) ngay sau khi hollowing thành công: thường thoát trong vài mili giây sau khi khởi động target process. Hollowed process không thừa hưởng quan hệ parent-child ngoài quá trình spawn suspended ngắn ngủi.

---

## Những gì KHÔNG có trong kiến trúc này

Để đầy đủ, các tính năng thường gặp ở cheat khác nhưng thứ này không có:

- Không DLL injection. Chỉ process hollowing thuần.
- Không driver / kernel component.
- Không đọc/ghi memory process game.
- Không hook game API.
- Không overlay màn hình.
- Không networking ra ngoài (chỉ LAN UDP tới Arduino + LAN TCP cho WebUI).
- Không telemetry / phone-home.
- Không auto-update.
- Không anti-VM (cố ý - VM cũng là môi trường dev/test hợp pháp).

Triết lý phòng thủ là trông giống một background helper process bình thường nhất có thể, làm ít thứ quan sát được nhất có thể, với toàn bộ input liên quan detection đi qua hardware HID thay vì software.
