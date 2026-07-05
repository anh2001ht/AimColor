# Coloruino - Security & Threat Model

Hệ thống này phòng thủ trước gì, không phòng thủ trước gì, và vì sao.

> Vanguard là closed-source. Mọi thứ bên dưới là phỏng đoán tốt nhất dựa trên
> writeup reverse engineering công khai, bài viết UnKnoWnCheaTs về phân tích
> thống kê Raw Input, và source của tool mouse-polling-monitor. Hãy xem đây
> là mô hình làm việc, không phải đảm bảo.

---

## ĐỌC PHẦN NÀY TRƯỚC - Xếp hạng rủi ro theo mode

Vanguard phân tích thống kê stream mouse-input của bạn theo rolling window
(community testing đặt khoảng **100 event**). Pattern lập trình - burst-fire
tập trung, profile chuyển động giống hệt, thiếu micro-jitter khi click -
hiện ra như anomaly so với baseline "người đang di chuột", NGAY CẢ KHI
từng packet riêng lẻ trông ổn.

| Mode | Rủi ro | Vì sao |
|------|--------|--------|
| Aimbot (`apply_delta`) | **THẤP** | Tracking liên tục, mượt, hòa vào chuyển động chuột thường, được sub-step với timing jitter ở firmware. |
| Triggerbot (`trigger_action`) | **TRUNG BÌNH** | Một click đơn gắn với phím trigger chủ động. Trông như click tay rất nhanh. Phân bố hold 20-67 ms đồng nhất có thể nhận diện qua nhiều sample nhưng từng click riêng lẻ vẫn qua. |
| Silent aim (`mode_a`) | **CAO** | Bốn HID report liên tiếp: snap + click + release + snap-back. Hình dạng deterministic. Burst tập trung dưới 5ms. Rolling-window analyzer của Vanguard CÓ bắt pattern này khi đủ sample. **ĐÃ KHIẾN TÔI BỊ BAN NHIỀU LẦN.** |
| Flickbot (`nonmode_a`) | **CAO** | Ba HID report liên tiếp: snap + click + release. Cùng hình dạng và rủi ro như silent. |

**Vì sao silent và flickbot ồn nhất:**

- Chúng gọi `Mouse.report()` 3-4 lần liên tiếp không có jitter giữa các lần.
- Vector chuyển động deterministic (không có variance kiểu người).
- Chúng tạo cluster mouse event chặt, ở mức thống kê trông không giống flick tự nhiên.
- Chúng CỐ Ý không humanize trong firmware - thêm latency sẽ phá cảm giác snap.
- Phân tích rolling 100-event của Vanguard SẼ thấy cluster shape nếu đủ sample.

**Nếu muốn sống lâu:**

- **Giới hạn cooldown của silent** ở mức thực tế (lệnh `K` đặt `P_COOLDOWN`; default 50 ms nhưng 200-500 ms khi chơi thật an toàn hơn).
- **Đừng dùng silent hoặc flick cho mọi pha giao tranh.** Trộn raw aim, bắn miss, tự flick thật.
- **Đừng tự tin quá sau một chuỗi one-tap lộ liễu.** Mật độ pattern trong cả match quan trọng ngang từng event riêng lẻ.
- Một match với kill silent-aim quá rõ có thể flag bạn dù mọi thứ khác sạch.
- **Nếu không sẵn sàng tự throttle, để silent và flickbot OFF, chỉ dùng aimbot + triggerbot.**

Đây là phần đã khiến tôi bị ban. Hai lần. Hãy nghiêm túc.

---

## Threat model

### Ngữ cảnh triển khai

Triển khai mặc định là **1PC**: Vanguard, Valorant, loader, ứng dụng PC đã
hollow, và USB endpoint của Arduino đều nằm trên cùng máy. Không có cô lập
vật lý giữa cheat và anti-cheat. Phòng thủ là **defense-in-depth trên một máy**.

Tùy chọn hardening 2PC (capture card + cheat PC riêng) chuyển ứng dụng cheat
ra khỏi GAME PC hoàn toàn. Trong layout đó, process scan, module scan,
liệt kê listening socket và behaviour monitoring của Vanguard trên GAME PC
không thấy gì. Threat model bên dưới nói về 1PC; 2PC giống vậy trừ mọi thứ
Vanguard có thể thấy ở phía cheat (không loader process, không WebUI port,
không UDP sender) vì không có thứ nào chạy trên GAME PC.

### Đối thủ

- **Riot Vanguard** - kernel anti-cheat. Chạy như driver Windows
 (`vgk.sys`) load lúc boot, cộng service user-mode (`vgc`). Active
 trên client PC. Có full visibility ring-0 vào mọi thứ trên máy đó.
- **Windows Defender / SmartScreen** - scanner thụ động trên client PC.
- **Người quan sát** nhìn màn hình client, Task Manager, Properties dialog,
 hoặc netstat - thường là chính client.

### Năng lực đối thủ (giả định)

Vanguard có thể trên client PC:
- Liệt kê mọi process đang chạy.
- Inspect mọi module loaded trong mọi process.
- Đọc memory page của process (kể cả payload hollowed).
- Hook syscall + quan sát NT calls.
- Inspect USB device descriptor của thiết bị đang cắm (kể cả Arduino).
- Capture và phân tích thống kê timing RawInput trên mọi thiết bị HID kết nối.
- Pattern-match chữ ký known-bad (DLL hash, function prologue,
 string lookup, code section CRC) với bất kỳ process nào trên máy.
- So sánh integrity process với expectation của signed binary (với process whitelisted - dù phần lớn random hollow target không whitelisted).
- Inspect local socket, listening port, và outbound LAN UDP.
- Ban HWID trên nhiều Riot account dùng phần cứng này.

Vanguard có lẽ KHÔNG THỂ (hoặc chưa quan sát thấy):
- Vươn qua Ethernet để inspect firmware hoặc flash contents của Arduino.
- Modify thiết bị không expose HID/CDC interface có khả năng ghi.
- Đọc hoặc capture dữ liệu cheat *chỉ giữ trong RAM rất ngắn rồi zero*
 (ví dụ license đã decrypt, payload đã decrypt trong lúc hand-off hollow -
 cả hai bị wipe trong vài mili giây).
- Decrypt payload mã hóa bên trong `AMDRSHelper.exe` trên đĩa nếu chưa lấy
 license + build salt bake trong loader.
- Phân biệt giữa chuột gaming thật bạn mimic và USB descriptor mimic của ta
 Ở MỨC DESCRIPTOR. (Full HID report descriptor byte có thể khác; xem
 "Detection vectors we don't address" bên dưới.)

### Tài sản

- License key (độ bí mật cao - gate build chain).
- AES salt + IV lúc build (trung bình - recover được bằng reversing loader,
 nhưng rotate mỗi build giới hạn blast radius).
- Image ứng dụng PC (cao - chứa mọi chữ ký detectable; không bao giờ nằm
 plaintext trên đĩa).
- Riot account của người dùng (cao nhất - ban là chi phí thực sự).

---

## Bản đồ phòng thủ (theo binary)

### coloruino-loader (AMDRSHelper.exe)

| Defense | Làm gì | Không làm gì |
|---|---|---|
| Sanitize_pe | Xóa Rich header + timestamp + debug dir, randomize section names, tính lại checksum | Không đổi import table - import `kernel32` / `ntdll` vẫn cho thấy đây là binary Windows |
| Dynamic WinAPI | Toàn bộ WinAPI liên quan hollowing (`CreateProcess`, `VirtualAllocEx`, v.v.) resolve qua `GetProcAddress` với tên xorstr-obfuscated | Không giấu LoadLibrary, không giấu việc GetProcAddress bị gọi nhiều lần |
| xorstr literals | Mọi string dùng (registry path, function name, file path) XOR-encrypt at rest, decrypt lười | Khi decrypt trong memory, string là plaintext cho tới khi `SecureZeroMemory` |
| AES-256-CBC payload | Byte ứng dụng PC nhúng được AES-encrypt với key dẫn xuất từ license người dùng + random theo build | Không giúp khi loader đang chạy - payload LÀ plaintext trong memory lúc hollow |
| TLS callback anti-debug | Hook lúc DLL-load-time trigger nếu debugger attach | Bị bypass tầm thường bởi kernel-mode anti-anti-debug |
| Multi-thread watchdog | Kiểm tra định kỳ (BeingDebugged, IsDebuggerPresent, hardware breakpoint, NtGlobalFlag, Heap Flags, DebugPort/DebugObjectHandle) | Chỉ detect debugger user-mode phổ biến (x64dbg, Cheat Engine, OllyDbg). Kernel debug + instrumentation kiểu VBox vô hình. |
| Random target | Chọn victim process khác mỗi lần launch | Bản thân loader process vẫn quan sát được trong cửa sổ ngắn nó chạy |
| 8-attempt retry | Nếu một pick ngẫu nhiên hollow không sạch, pick process khác | Không giúp nếu MỌI process loader thấy đều có EDR shielding |
| Self-signed Authenticode | Properties dialog đọc như publisher AMD đã verify (trên máy đích đã cài root cert) | Không bypass heuristic hành vi của Defender - signed không có nghĩa safe |
| AMD masquerade | Binary metadata (FileDescription, ProductName, CompanyName) đều đọc là AMD Radeon Software | Subject signing cert cũng đọc AMD - nhưng self-signed; analyst kiểm tra chain sẽ thấy "issued by self" |

### coloruino-app (pipanel.exe)

| Defense | Làm gì | Không làm gì |
|---|---|---|
| Process hollowing | Code chạy trong PID của process khác, không phải PID riêng | Vanguard thấy hollowed process hành vi dị thường (memory layout, RWX section, thread anomaly) - phân tích tinh vi sẽ bắt |
| /MT static CRT | Không phụ thuộc CRT DLL - load ít DLL hơn app thường | Không giấu được static CRT functions trong image |
| No PDB / no debug info | Strip symbols | Function prologue + control flow vẫn recover được qua IDA / Ghidra |
| Windows subsystem (no console) | Không flash console window | Nếu Vanguard enumerate subsystem sẽ thấy Windows app, loại phổ biến nhất |
| HWID-bound license check | File `data` encrypt theo HWID máy hiện tại; mismatch = thoát im lặng | Không giúp khi `data` đã tạo đúng - binary vẫn chạy cheat |
| Anti-debug copy | Cùng multi-thread watchdog như loader | Cùng hạn chế |
| Defense-in-depth license check | Đọc `data`, validate HWID hash, mismatch thoát im lặng | Bypass được bởi người có debug access |
| WebUI behind Basic auth | Panel điều khiển qua phone cần user/pass | Chỉ là credential barrier - credentials bake trong binary |
| WebUI port 13548 | Trên vùng well-known, không conflict IIS/Apache | Port scan trong LAN thấy ngay. Vanguard cũng thấy local listener. |
| DNS-shape UDP | Outbound packet tới Arduino nhìn thoáng như mDNS | Deep packet inspection thấy qname là gibberish mã hóa |
| xorstr literals | Giống loader | Giống |

### coloruino-fw (firmware Leonardo)

| Defense | Làm gì | Không làm gì |
|---|---|---|
| USB descriptor mimicry | Report VID 0x???? / PID 0x???? (danh tính chuột gaming spoof, đặt theo build) - trông như chuột gaming thật | USB packet capture lúc enumeration cũng thấy manufacturer + product string placeholder khớp - nhưng vendor placeholder có HID report manufacturer-defined cụ thể mà ta có thể replicate đúng hoặc không |
| MAC OUI rotation | Ethernet shield dùng một trong 6 vendor OUI (Intel, Realtek, Apple, Samsung, Microsoft, Dell), rotate mỗi boot | Chỉ OUI thay đổi; tail 3 byte của thiết bị cố định |
| Velocity-curve sub-stepping | Movement lệnh M chia qua nhiều USB poll với ease-in/ease-out timing | Ở RawInput host, sub-step gộp thành một report mỗi USB poll (~1 ms); variation timing nội bộ không nhìn thấy |
| Bimodal jitter | 94% 150-405 us / 6% 1000-1255 us inter-step delay | Tương tự - timing nội bộ Arduino, host không thấy |
| L command randomization | Click hold time uniform 20-67 ms | Analyzer thống kê với nhiều sample click có thể nhận diện hình dạng phân bố uniform |
| P / F handlers UNCHANGED | Silent aim và flicker bắn nguyên trạng, không humanization | Có chủ đích - thêm humanization vào P/F làm hỏng cảm giác aim |
| HID interface order forced | Mouse interface đăng ký trước keyboard (khớp thứ tự descriptor thiết bị thật spoofed) | Thiết bị chỉ có chuột nhưng enumerate hai interface là fingerprint - đã fix nhưng inherent |

---

## Vanguard thấy gì trên PC chơi (mode 1PC)

Vì mọi thứ nằm trên một máy ở layout 1PC mặc định, Vanguard thấy toàn bộ bên dưới. Ở layout 2PC tùy chọn, mục 3-9 biến mất khỏi GAME PC (đều sống trên CHEAT PC), chỉ còn mục 1-2 nhìn thấy với Vanguard.

1. **Một thiết bị USB** tự nhận là chuột gaming thật bạn mimic.
 VID/PID, manufacturer string, product string, device descriptor đều
 nhất quán. HID *report* descriptor byte là descriptor Arduino
 Leonardo stock, KHÔNG phải của vendor placeholder thật - đây là
 mismatch có thể fingerprint nếu Vanguard byte-compare với reference
 known-good.
2. **RawInput event ở 1000 Hz** (poll rate khai báo) với stddev jitter
 dưới mili giây. **ĐÂY LÀ EXPOSURE CHÍNH** - xem phần mouse-polling-monitor
 bên dưới. Vanguard thấy stream này từ góc nhìn process game.
3. **Một binary signed `AMDRSHelper.exe`** nằm trên đĩa với publisher
 "AMD Radeon Software". Self-signed - chain không validate với CA store
 của Microsoft, nhưng CÓ validate local nếu root cert đã cài trong setup.
 Vanguard biết legitimate AMD publisher và có thể so sánh; của ta không khớp.
4. **Một loader process** (ngắn) tên `AMDRSHelper.exe` decrypt
 ~5-15 MB data nhúng và ngay lập tức chạy sequence process-hollowing
 lên một victim ngẫu nhiên. Pattern
 `CreateProcess(SUSPENDED) -> VirtualAllocEx -> WriteProcessMemory
 sections -> SetThreadContext -> ResumeThread` rất quen thuộc với mọi
 EDR hiện đại và gần như chắc với Vanguard.
5. **Hollowed target process** (bất kỳ thứ loader chọn - ví dụ
 `dllhost.exe`) có memory layout dị thường: section protection không
 khớp on-disk image, thread không tới từ entry point PE gốc, không symbol
 resolution cho image "loaded". Kernel scan sẽ flag.
6. **Listening TCP port 13548** với firewall rule "AMD Radeon
 Software Helper". Thấy được qua local socket enumeration.
7. **Outbound UDP traffic** tới bất kỳ IP nào của Ethernet shield Arduino
 (default `192.168.1.216:5353`). Nhìn thoáng như mDNS; deep packet inspection
 thấy encrypted qname gibberish.
8. **Một Ethernet adapter** trên client PC có IP tĩnh cùng subnet với shield
 Arduino. Bất thường trừ khi client có lý do hợp pháp (có thể có -
 PC nhiều NIC khá phổ biến).
9. **Không autorun entry**, không service cài, không registry trail.
 `AMDRSHelper.exe` chỉ chạy khi user (hoặc shortcut tự thêm vào Startup) start.

---

## Rủi ro mouse polling monitor

Tool C# WPF tại `extra/mouse-polling-monitor/` reverse engineer heuristic có thể của Vanguard:

```
verdict = INJECTION if stddev <= 0.015 ms OR burst-rate >= 20%
verdict = SUSPICIOUS if stddev <= 0.060 ms OR burst-rate >= 10%
verdict = NATURAL otherwise
 burst = consecutive movement packets <0.5 ms apart
 window = last 100 movement packets
 min samples = 30
```

Firmware hiện tại:
- Burst rate: **0%** - USB poll giới hạn ở 1 ms, không có burst ở host level. Đạt.
- StdDev: **10-50 us** - chỉ còn USB scheduling jitter, vì internal Arduino
 sub-step jitter (150-405 us) bị USB layer coalesce. Cận ngưỡng tới flagged.

Vector burst-rate ta qua sạch. Vector stddev là exposure: trên máy sạch không có traffic USB khác, stddev của ta có thể rơi vào vùng INJECTION (<=15 us). Trên máy bận, nó nổi lên vùng NATURAL.

**Mitigation (deferred, cần user OK)**: tweak firmware Phase 3.1.
Trong handler lệnh M, thay đổi số USB poll boundary chờ giữa emission sub-step.
80% emit ở poll kế, 15% skip 1, 5% skip 2-4. Tạo mean ~1.3 ms,
stddev ~0.4-0.6 ms - cao hơn mọi threshold.

Có cần không: chạy polling monitor với firmware live trước. Nếu verdict NATURAL,
để firmware yên.

---

## Detection vectors chưa xử lý

Đây là những vùng Vanguard có thể bắt coloruino mà ta không cố giấu:

### Phân tích thống kê movement-pattern

Flick aim thật của người có profile gia tốc đặc trưng. Tay tăng tốc, giảm tốc,
micro-correct. Sub-step lệnh M của ta theo velocity curve nhưng là cùng curve
mỗi lần. Classifier có đủ sample có thể phân biệt.

### Cross-correlation giữa mouse và click

Người thường có micro-jitter vị trí chuột khi bấm nút. Handler silent aim của
Coloruino thì không - click là burst sạch, không jitter trên nút held.
Phân tích correlation thống kê giữa stream mouse và click có thể flag.

### Pattern shift cấp session

Khi cheat bắn, phân bố timing của mouse event đổi đột ngột. Classifier nhìn
vào *transition* giữa idle và aim-burst có thể detect discontinuity dù từng state
riêng lẻ trông human.

### So sánh USB descriptor full-fidelity

Vanguard có thể lấy HID report descriptor và so byte-for-byte với descriptor
vendor placeholder known-good. Ta mimic device descriptor nhưng HID report
descriptor là Arduino Leonardo Mouse stock - không phải vendor placeholder thật.
Mismatch.

### Memory page protections

Process hollowing tạo pattern dị thường cụ thể: process created suspended,
section viết trước resume, EIP set từ bên ngoài. EDR mới (và có lẽ Vanguard)
detect trực tiếp qua kernel callback trên `NtProtectVirtualMemory` và
`NtSetContextThread`.

### Hành vi dưới tải

CPU usage của Coloruino nhỏ nhưng có đặc trưng - DXGI call, LUT scan lớn,
UDP sendto định kỳ. Behaviour profile có thể nhận diện.

---

## Vòng đời secrets

### License key

- **Source**: bạn chọn lúc build, embed dạng FNV-1a hash trong loader / app / config-gen.
- **Xuất hiện ở đâu**: user nhập một lần vào config-gen, một lần vào dialog loader
 (hoặc một lần vào `auth.dat`).
- **Rotation**: mỗi release. Recompile mọi thứ bake hash.
- **Tác động nếu compromise**: người khác có thể redeem license trên máy họ
 nếu có binaries. HWID binding giới hạn nhưng không ngăn tuyệt đối.

### Build salt (`kBuildSalt`, 32 byte)

- **Source**: `secrets.token_bytes(32)` mỗi build qua gen_build_secrets.py.
- **Xuất hiện ở đâu**: chỉ trong loader binary, chỉ dưới dạng `build_secrets.h`
 lúc compile.
- **Rotation**: mỗi build (tự động - vcxproj PreBuildEvent).
- **Tác động nếu compromise**: người reverse loader có thể derive payload AES key
 với license. Tầm thường - nhưng chỉ quan trọng nếu license cũng leak.

### Payload IV (`kPayloadIV`, 16 byte)

- **Source**: giống `kBuildSalt`.
- **Ở đâu**: `build_secrets.h`.
- **Rotation**: mỗi build.
- **Tác động nếu compromise**: riêng IV không có gì - IV không phải secret. Cần key để decrypt.

### HWID salt (`kHwidSalt`, 12 byte)

- **Source**: constant hardcoded khớp config XOR key.
- **Ở đâu**: loader `hwid.cpp`, app `LicenseManager.cpp`, config-gen.
- **Rotation**: qua `rotate_secrets.py` (cùng config XOR key).
- **Tác động nếu compromise**: cho attacker rebuild HWID hash từ raw machine info -
 có thể forge `auth.dat` / `data`. Tác động trung bình.

### WebUI credentials

- **Source**: `rotate_secrets.py` chọn cặp random.
- **Ở đâu**: app `Auth.cpp`.
- **Rotation**: mỗi release.
- **Tác động nếu compromise**: chỉ LAN access - đã cần ở trong home network của user.
 Phone bookmark + bookmark file có thể leak cả hai. Rủi ro chấp nhận được.

### Protocol XOR key (16 byte)

- **Source**: `rotate_secrets.py`.
- **Ở đâu**: app `UDPClient.cpp`, firmware `coloruino-fw.ino`.
- **Rotation**: mỗi release. Cả PC và firmware phải rebuild + redeploy.
- **Tác động nếu compromise**: người capture LAN UDP có thể decrypt payload format.
 Đã ở LAN, vốn là mối quan tâm thấp.

### Signing private key (`code_signing.pfx` + `.password.txt`)

- **Source**: `01_generate_cert.ps1` một lần.
- **Ở đâu**: chỉ máy build. Gitignored.
- **Rotation**: khi nghi compromise HOẶC vài năm một lần.
- **Tác động nếu compromise**: attacker có thể ký binary riêng mà đọc như
 "AMD Radeon Software" trên máy đích trust cert. Đáng kể - nhưng cert cũng
 self-issued, nên chỉ hiệu lực trên máy bạn kiểm soát.

---

## Rotate gì, khi nào

| Trigger | Rotate |
|---|---|
| Game patch mới | không tự động - chờ xem detection vector có đổi không |
| Nghi bị detect | license, payload AES, signing identity (mọi thứ) |
| Chia sẻ binary với user mới | license + WebUI creds (để họ có scope riêng) |
| Vệ sinh định kỳ (hằng tháng?) | license + WebUI creds |
| Máy build compromise | signing identity (.pfx) |
| `data` hoặc `auth.dat` hỏng (không nghi compromise) | không rotate gì - chỉ regenerate file bị ảnh hưởng |

---

## Investigator sẽ thấy gì

Nếu forensic analyst có năng lực lấy được binaries:

1. `AMDRSHelper.exe` trông như binary AMD Radeon ký bởi publisher đáng ngờ.
 Signing cert self-signed (không chain tới CA thật) - red flag đầu tiên.
2. Static analysis loader thấy:
 - Dùng `GetProcAddress` nặng để resolve runtime API (red flag -
 binary AMD hợp pháp link tĩnh các hàm này).
 - Blob encrypted nhúng ~5 MB (red flag - giống packed payload).
 - Marker VMProtect trong code (nếu dùng VMProtect) hoặc routine anti-debug rộng
 (cũng red flag).
3. Binary không có Rich header, section name random, TimeDateStamp bằng 0 -
 đều khớp intentional obfuscation.
4. Dynamic analysis: chạy dưới debugger trigger anti-debug, detect debugger và thoát.
 Không có debugger thì chạy thành công.

Analyst có động lực sẽ biết đây là cheat trong vài giờ. Phòng thủ của ta là volume:
đa số observer (Riot triage analyst, classifier tự động, AV scanner) không có vài giờ
cho mỗi sample. Mục tiêu là đủ nhàm chán ở cái nhìn đầu để không đáng deep dive.

---

## Failure modes (thứ khiến ta bị bắt, theo xác suất)

1. **Vanguard update** với heuristic mới nhắm kỹ thuật cụ thể của ta
 (process hollowing pattern, DXGI sustained capture, v.v.). Mitigation:
 rotate sang kỹ thuật mới.
2. **HWID ban** từ vi phạm trước trên cùng máy. Không có phòng thủ -
 đổi mainboard hoặc ngừng chơi account đó.
3. **Statistical mouse pattern detection** ở mức tool mouse-polling-monitor
 mô phỏng. Mitigation: tweak firmware Phase 3.1.
4. **Chia sẻ binary bị leak** - bạn bè bị ban, crash dump / file submission
 tới Riot, pipeline analysis của Riot nhận diện build của bạn. Mitigation:
 đừng chia sẻ, hoặc rotate mạnh nếu chia sẻ.
5. **Defender / cloud AV** flag loader khả nghi dựa trên entropy/behavior.
 Mitigation: signing giúp; Defender exclusion giúp hơn.
6. **Bạn mắc lỗi** - để debugger attach trong match, để console window mở,
 dùng WebUI từ điện thoại của bạn bè đang đăng nhập Riot account của bạn, v.v.

---

## Ngoài phạm vi, lựa chọn có chủ ý

- Không VM/sandbox detection. Ta không tránh VM vì VM cũng là nơi dev/test hợp pháp.
- Không so sánh license constant-time chống timing attack. License check dùng
 FNV-1a không constant-time. Người chạy loader dưới instrumentation có thể
 side-channel license. Chấp nhận điều này - loader side-channelable không giúp
 Vanguard bên ngoài.
- Không bảo vệ trước memory scraper chạy trên cùng máy. Nếu thứ gì có privilege
 đọc process memory của ta, nó có tất cả. Ta tập trung vào threat model
 network-and-USB-isolated.
- Không anti-WoW64. Ta chỉ 64-bit; nếu ai đó chạy dưới WoW64 thì họ đang cố debug,
 và ta muốn fail.

---

## Nếu bạn tìm thấy bug hoặc detection thật

1. Đừng tiếp tục chơi trên account bị ảnh hưởng.
2. Capture: build nào (timestamp `rotate_secrets`), Windows version nào,
 Vanguard version nào (từ service description `vgc`), loader chọn target process nào.
3. Ghi rõ symptom: nội dung email ban, mã lỗi Vanguard cụ thể, v.v.
4. Rotate mọi thứ trước release tiếp theo.
5. Xem failure chỉ ra một defense cụ thể cần thay thế, hay detection chung nghĩa là
 kỹ thuật đã cháy.
