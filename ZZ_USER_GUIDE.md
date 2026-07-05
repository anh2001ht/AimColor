# Coloruino - Hướng dẫn người dùng

Hướng dẫn bằng ngôn ngữ dễ hiểu. Không code, không thuật ngữ khó nếu bạn không muốn đọc. Tài liệu kỹ thuật nằm ở [ZZ_ARCHITECTURE.md](ZZ_ARCHITECTURE.md). Các bước build đầy đủ nằm ở [ZZ_BUILD_GUIDE.md](ZZ_BUILD_GUIDE.md).

---

## Đây là thứ gì

Một chương trình nhỏ quan sát màn hình của bạn. Khi viền kẻ địch hiện màu tím (màu highlight địch mặc định của Valorant), chương trình quyết định nên aim vào đâu và bảo một board điện tử nhỏ gọi là Arduino làm việc. Arduino cắm vào PC và giả vờ là một chuột USB. Từ góc nhìn của hệ điều hành và Valorant, một con chuột gaming bình thường đang thực hiện mọi chuyển động.

Không có DLL inject vào Valorant, không driver, không truy cập memory trong game, không kernel module. Code cheat không bao giờ chạm vào process của game. Input đi vào giống như từ bất kỳ chuột USB nào.

---

## Hai tùy chọn triển khai

### 1PC mặc định (đa số dùng)

Mọi thứ chạy trên MỘT PC Windows. Valorant chạy ở đó, cheat chạy ở đó, Arduino cắm vào PC đó. Setup đơn giản hơn, ít phần cứng hơn, chi phí thấp hơn. Đổi lại, Vanguard CÓ THỂ thấy process cheat và WebUI listener trên cùng máy - thiết kế làm chúng trông vô hại nhất có thể (loader được sanitize, metadata mang thương hiệu AMD, binary signed, hollow vào một target process hợp pháp) nhưng chúng vẫn quan sát được.

### 2PC tùy chọn (an toàn hơn, nhiều phần cứng hơn)

Hai PC: GAME PC chạy Valorant + Vanguard, và CHEAT PC chạy ứng dụng cheat. Capture card trên CHEAT PC đọc màn hình GAME PC qua HDMI. Arduino cắm USB vào GAME PC để xuất USB HID nhưng nhận UDP từ CHEAT PC qua Ethernet.

Kết quả: Vanguard scan GAME PC và không thấy GÌ liên quan đến coloruino. Không process, không DLL, không listening socket, không binary khả nghi. Đổi lại là chi phí của PC thứ hai + capture card passthrough + nhiều dây hơn.

Phần mềm giống hệt trong cả hai setup. Hướng dẫn này nói về 1PC; với 2PC chỉ cần đặt `AMDRSHelper.exe` trên máy cheat, gán NIC của máy cheat một IP nằm trong subnet của Arduino, và cắm USB của Arduino vào máy chơi game.

---

## PC build và PC chơi

Bạn nên LUÔN build trên một PC khác với PC dùng để chơi. PC build có Visual Studio, Python, Arduino IDE, HxD, tùy chọn VMProtect. Những thứ đó không nên nằm trên PC chơi. Build trên máy A, copy `AMDRSHelper.exe` sang máy B, xong.

Điều này vẫn đúng ngay cả khi phần chơi dùng mode 1PC. "1PC" nói về máy chơi; máy build là một chuyện riêng.

---

## Bạn cần gì

**Phần cứng:**

- Một Arduino Leonardo (hoặc bất kỳ board ATmega32U4 nào).
- Một USB Host Shield 2.0.
- Một W5500 Ethernet shield.
- 6 dây jumper ngắn.
- Hai cáp USB (một để lập trình/HID cho Arduino, một để passthrough chuột thật).
- Một cáp Ethernet từ NIC của PC tới W5500.

**Phần mềm:**

- `AMDRSHelper.exe` (binary duy nhất nằm trên PC chơi).
- Firmware Arduino đã flash vào board (làm lúc build, không phải lúc cài).

**Thông tin cần biết:**

- License key 32 ký tự (hex lowercase). Bạn đặt giá trị này lúc build.
- Username + password Basic-auth WebUI bạn đặt lúc build.

---

## Setup phần cứng

Đi dây là phần khó nhất. W5500 cần SPI nhưng header pin của USB Host Shield che vật lý các chân ICSP của Arduino. Cách xử lý: kẹp dây jumper từ ICSP của Arduino đi lên, băng qua USB Host Shield tới W5500.

Ảnh chi tiết + từng bước nằm ở thread UC cũ của tôi:

> **https://www.unknowncheats.me/forum/4078059-post1.html** - hướng dẫn lắp phần cứng + đi dây có hình.

Và nền tảng chung về phần cứng cheat dựa trên Arduino (cũ hơn nhưng đúng về ý tưởng):

> **https://www.unknowncheats.me/forum/4093793-post1.html** - hướng dẫn Arduino cheating tất-cả-trong-một.

Các chân firmware mong đợi:

| W5500 pin | Arduino pin |
|-----------|-------------|
| MOSI | ICSP-4 |
| MISO | ICSP-1 |
| SCK | ICSP-3 |
| 5V | 5V |
| GND | GND |
| CS | D6 |
| RST | D7 |

Khi stack đã nối dây:

- Cắm cáp USB chính của Arduino vào PC. Đây là thứ đóng vai trò chuột gaming giả lập (cách Valorant nhận HID input).
- Cắm cáp Ethernet từ W5500 của Arduino tới NIC phụ trên PC. Đây là đường ứng dụng cheat gửi lệnh UDP.
- Cắm chuột THẬT của bạn vào cổng USB của USB Host Shield (KHÔNG cắm trực tiếp vào PC). Firmware chuyển tiếp trạng thái nút bấm của nó.
- Đặt IP tĩnh trên NIC phụ, cùng subnet với Arduino (placeholder mặc định 192.168.1.x; firmware ship với 192.168.1.216).

---

## Cài đặt lần đầu

1. Copy `AMDRSHelper.exe` vào đâu đó trên PC chơi. Vị trí không quá quan trọng; `C:\Program Files\AMD\CNext\CNext\` khá hòa với cài đặt AMD thật.
2. (Tùy chọn) Cài root cert ký nếu build chain của bạn tạo ra. Right-click `AMDRSHelper.exe` -> Properties -> Digital Signatures - sau khi cài cert sẽ thấy publisher là "AMD Radeon Software" và không cảnh báo. Nếu không, Defender xử lý binary signed giống unsigned.
3. Double-click `AMDRSHelper.exe`. Một dialog nhỏ hiện ra hỏi license key. Paste vào. Click OK.
4. Nếu mọi thứ nối dây đúng:
   - Dialog biến mất.
   - Con trỏ chuột giật rất nhẹ (tín hiệu "tôi đang sống" của ứng dụng).
   - Hai file xuất hiện cạnh exe: `auth.dat` (license đã nhớ, mã hóa) và `data` (config mã hóa với HWID hash của máy).
5. Mở `http://localhost:13548/` trong trình duyệt. Nhập thông tin Basic-auth WebUI bạn đặt lúc build. Bạn sẽ thấy panel cấu hình.
6. Nhấn nút **Test** ở đầu panel. Con trỏ chuột nên giật một lần - xác nhận toàn chuỗi hoạt động (cheat app -> UDP -> Arduino -> HID -> OS).

---

## Dùng hằng ngày

Sau khi cài:

1. Bật PC.
2. Double-click `AMDRSHelper.exe` (hoặc đặt vào Startup folder để chạy cùng Windows). Không hỏi license nữa - `auth.dat` xử lý phần đó.
3. Đảm bảo Arduino đang cắm (USB + Ethernet).
4. Khởi động Valorant.
5. Chơi.

Để chỉnh ngay khi đang dùng, mở WebUI trên điện thoại hoặc trình duyệt và kéo slider. Thay đổi được lưu tức thì. Không cần restart.

Để tắt tạm một mode, mở WebUI -> tab của mode đó -> gạt "Enabled" off.

---

## WebUI - từng mục làm gì

### Tab Aimbot (rủi ro THẤP)

Tracking liên tục khi giữ phím. Mượt, hòa vào chuyển động chuột bình thường.

- **Enabled** bật/tắt.
- **FOV** bán kính tìm kiếm tính bằng pixel quanh crosshair.
- **Smooth** càng cao = tracking càng lười, tự nhiên hơn.
- **Speed** càng cao = kéo về target nhanh hơn.
- **Sleep** số mili giây giữa các lần điều chỉnh.
- **Key 1 / Key 2** mã virtual key Windows (2=chuột phải, 1=chuột trái, 6=chuột giữa, 65=A, v.v.).

### Tab Silent (rủi ro CAO)

Snap một lần tới target + click + snap lại. Mode rủi ro nhất.

- **Head Targeting** chọn đỉnh silhouette hay pixel gần nhất.
- **FOV / Distance gain / Cooldown** cùng ý tưởng với aimbot.

CẢNH BÁO: Bộ phân tích rolling-window cho mouse event của Vanguard flag các pattern burst-fire tập trung. Silent aim bắn 4 HID report liên tiếp với hình dạng xác định - đó là chữ ký thống kê. Dùng hạn chế, cooldown cao, và không dùng cho mọi pha giao tranh. Xem [ZZ_SECURITY.md](ZZ_SECURITY.md) để biết breakdown rủi ro đầy đủ.

### Tab Flicker (rủi ro CAO)

Giống silent nhưng không snap-back. 3 HID report. Cùng profile rủi ro - xem ở trên.

### Tab Trigger (rủi ro TRUNG BÌNH)

Tự left-click khi crosshair nằm trên màu địch. Gắn với nhấn phím chủ động nên ít dị thường hơn silent/flick, nhưng vẫn là tự động hóa.

- **FOV X / FOV Y** kích thước box quanh crosshair để kiểm tra.
- **Polygon Check** khi bật, chỉ bắn nếu màu nằm giữa hình dạng địch (lọc false-fire từ UI một phía).

### Tab Head Anchor Refinement

Tinh chỉnh aim vào ĐÂU trên địch (trán, mũi, cổ, ngực). Đa số người dùng không đụng nữa sau khi tìm được setting hợp ý.

### Tab Filtering

- **Dead Body Filter** loại mục tiêu ragdoll đang trượt khỏi màn hình.
- **Cluster Validation** loại pixel nhiễu cô lập.

### Tab Color

Bốn preset màu LUT có thể nạp. Mặc định là **Purple** (màu outline địch của Valorant). Có biến thể cho ánh sáng hoặc tint skin bất thường.

### Tab Performance

- **GPU compute** chạy detection trên card đồ họa. Nhanh hơn nhưng giới hạn FOV 255 px mỗi trục. Bật mặc định.
- **Arduino Connection** IP + port nơi ứng dụng gửi UDP. Thay đổi được lưu tự động vào `data`. Ứng dụng reconnect khi đổi.

---

## Troubleshooting

### "Tôi chạy AMDRSHelper.exe nhưng không có gì xảy ra"

Mở Task Manager. Tìm `AMDRSHelper.exe` trong Background processes. Nếu không có:

- Kiểm tra thư mục có `data` và `auth.dat` chưa. Nếu chưa, loader chưa hoàn tất first-run. Chạy lại và nhập license.
- Windows Defender có thể đã ăn nó. Thêm folder exclusion trong Defender -> Virus & threat protection -> Manage settings -> Exclusions.
- Nếu bạn bỏ qua bước cài cert và behaviour monitor của Defender đang gắt, binary có thể bị kill âm thầm. Cài cert.

### "Loader cứ hỏi license mỗi lần chạy"

`auth.dat` không được đọc ở các lần chạy sau. Bản build hiện tại đáng lẽ đã fix (path resolve tương đối với vị trí exe). Nếu vẫn xảy ra, bạn đang chạy build cũ - rebuild từ source hiện tại.

### "Cursor giật nhưng không aim đúng"

- Preset màu sai với map/agent hiện tại? Test trong Custom game với outline địch màu tím sáng.
- FOV quá nhỏ cho khoảng giao tranh bạn đang bắn?
- Smooth/Speed quá thấp? Tăng lên.
- Setting head-anchor sai? Thử default.

### "Arduino có vẻ không phản hồi"

- Cả hai cáp đã cắm chưa? USB tới PC cho HID, Ethernet tới PC cho UDP.
- Trong URL bar của WebUI, nhập `/testing` (ví dụ `http://localhost:13548/testing`). Ứng dụng gửi một lệnh move nhỏ tới Arduino. Nếu cursor giật, chuỗi hoạt động. Nếu không, kiểm tra IP tĩnh của Arduino có khớp tab Performance của WebUI không.
- Chuột thật đã cắm vào USB Host Shield chưa (không cắm trực tiếp vào PC)?

### "Valorant báo lỗi anti-cheat / account bị ban"

Ngừng dùng tool trên account đó. Đọc [ZZ_SECURITY.md](ZZ_SECURITY.md) để hiểu threat model thực tế. Không setup nào được đảm bảo an toàn mãi mãi - Vanguard update, công nghệ detection phát triển.

Nếu bạn dùng silent hoặc flickbot nặng, gần như chắc đó là nguyên nhân. Chúng được gắn nhãn rủi ro CAO là có lý do.

### "Defender quarantine AMDRSHelper.exe"

Hai lựa chọn:

1. Thêm folder exclusion (Defender -> Manage settings -> Add exclusion).
2. Đảm bảo bạn đã cài signing cert lúc install - nếu không, Defender xử lý signed và unsigned như nhau.

### "WebUI chạy trên PC nhưng không chạy trên điện thoại"

Router của bạn chặn traffic từ điện thoại tới PC. Thường gặp trên Wi-Fi công ty, mạng công cộng, và một số mesh router (AP isolation). Trình duyệt local trên PC tại `http://localhost:13548/` luôn hoạt động.

### "Tôi quên password WebUI"

Đó là giá trị bạn đặt lúc build (xem source `coloruino-app/.../Auth.cpp` để biết bạn đã đặt gì). Nếu không có source bên cạnh, bạn cần rebuild với credentials mới.

---

## FAQ

**Q: License dùng để làm gì?**

A: Đây là key mở khóa giải mã code cheat nhúng bên trong `AMDRSHelper.exe`. Nếu không có license đúng, các byte đó chỉ là rác ngẫu nhiên, loader sẽ unpack rác và không inject được gì. License bị ràng buộc HWID qua `auth.dat`, nên copy binary sang PC khác mà không nhập lại license cũng không dùng được.

**Q: File `data` từ đâu ra?**

A: Loader ghi nó cạnh chính nó lần đầu nhập license thành công. Format giống hệt output của `config_generator.exe` (mã hóa bằng config XOR key, `LICENSE_HWID` ràng buộc máy này). `config_generator` độc lập vẫn tồn tại để debug phía supplier nhưng không nằm trong install thông thường.

**Q: Cái này đảm bảo không bị detect không?**

A: Không. Không cheat nào vậy. [ZZ_SECURITY.md](ZZ_SECURITY.md) ghi rõ thứ gì được che và thứ gì không. Vanguard update thường xuyên. Silent aim và flickbot là rủi ro CAO và ĐÃ khiến tôi bị ban.

**Q: Tôi có thể chạy setup 1PC hay 2PC không?**

A: Cả hai đều hoạt động. 1PC là mặc định (một máy chạy mọi thứ). 2PC với capture card an toàn hơn nhưng cần PC thứ hai + capture card. Xem [ZZ_ARCHITECTURE.md](ZZ_ARCHITECTURE.md) để biết wiring 2PC.

**Q: Nó auto-aim xuyên tường không?**

A: Chỉ khi bức tường màu tím. Hệ thống theo nghĩa đen chỉ thấy màu outline địch. Thứ không có outline là vô hình.

**Q: Ứng dụng có phone home không?**

A: Không. Mọi thứ local. Không telemetry, không license server, không auto-update. Network traffic duy nhất là LAN local (PC tới Arduino UDP + WebUI listener).

**Q: Latency thế nào?**

A: Tổng latency end-to-end từ "pixel đổi màu" tới "Arduino bắn mouse event" thường là 2-5 ms. Nhanh hơn phản xạ người (200+ ms) nhưng vẫn có thể nằm trong vùng chuột thật.

**Q: Tôi muốn đổi license / password WebUI / bất kỳ key nào**

A: Chạy `python rotate_secrets.py` trên PC build. Nó in ra giá trị mới và các file source cần paste vào. Rebuild, redeploy.

---

## Đọc tiếp ở đâu

- [ZZ_BUILD_GUIDE.md](ZZ_BUILD_GUIDE.md) - mọi flag, mọi setting config, mọi pre-flight check.
- [ZZ_ARCHITECTURE.md](ZZ_ARCHITECTURE.md) - screen capture, color detection, network protocol, process hollowing thực sự hoạt động thế nào.
- [ZZ_SECURITY.md](ZZ_SECURITY.md) - Vanguard thấy gì, ta phòng thủ trước gì, rủi ro còn lại là gì. **Đọc phần rủi ro silent/flickbot trước khi bật chúng.**
- `coloruino-fw/ZZ_README.md` - patch Arduino IDE board profile để spoof VID/PID/descriptor của chuột thật đã chọn.

---

## Khi bị kẹt

Cách phục hồi an toàn cho đa số lỗi config-corruption là chu trình sạch: xóa `data`, xóa `auth.dat`, chạy lại `AMDRSHelper.exe`, nhập lại license. Hai phút, sửa 90% lỗi bí ẩn.
