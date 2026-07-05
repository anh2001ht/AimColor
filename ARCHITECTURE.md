# Coloruino - System Architecture

System-wide technical reference. For per-component internals see each component's own README under its folder.

---

## Deployment models

### Default - 1PC

Everything runs on ONE PC. Valorant + Vanguard + AMDRSHelper.exe + the Arduino plugged in. The Arduino's USB cable is the OS's mouse input; the Arduino's Ethernet cable receives UDP from the cheat application running on the same PC.

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

### Optional - 2PC + capture card

Higher stealth at the cost of hardware and cabling. AMDRSHelper.exe runs on a CHEAT PC that never touches Valorant. The CHEAT PC sees the GAME PC's screen through a capture card. The Arduino has its USB plugged into the GAME PC (HID output) and its Ethernet plugged into the CHEAT PC (UDP input).

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

In 2PC mode the GAME PC has zero coloruino software. Vanguard's process scans, module scans, listening-socket enumeration, and behaviour monitoring on the GAME PC come up empty. The CHEAT PC has all the cheat code, but Vanguard never runs there.

Code-wise nothing changes between 1PC and 2PC - same binaries, same firmware, same wire protocol. Just a different physical cable layout. This document covers the 1PC default; 2PC is the same minus the Vanguard view of the cheat process.

### Build PC

The build PC (Visual Studio, Python, VMProtect, HxD, Arduino IDE) should be SEPARATE from the play PC regardless of which deployment you use. Dev tooling, IDE telemetry, PDB files, and registry footprint should not sit on a box that Vanguard scans.

---

## The four components

### coloruino-config-generator

Standalone CLI tool. In the released setup this is OPTIONAL - the loader's `data_writer.cpp` does the same job at first-run. Kept in the repo as a supplier-side debug tool for forcing a fresh `data` without going through the loader's license prompt.

Inputs:
- License key (argv[1]).
- Current machine's HWID (computed on the spot).

Outputs:
- `data` file, XOR-encrypted with the build-time XOR key, containing:
  - Arduino IP and port.
  - `LICENSE_HWID=<hash>`.
  - `---CONFIG_START---` separator.
  - All `cfg::*` runtime variables with defaults.

Validates the license against its own FNV-1a constant before producing output. Wrong license = no `data` written, silent exit.

### coloruino-loader (AMDRSHelper.exe)

The visible binary on the play PC. Runs at user level, no elevation required.

Responsibilities:

1. Anti-debug install (`antidebug::install()`).
2. Dynamic WinAPI resolution (`winapi::init()`). All WinAPI functions used by the hollowing path are resolved via `GetProcAddress` with xorstr-obfuscated names. Never reached as static imports.
3. License acquisition. Read `auth.dat` (HWID-encrypted), else prompt via a native modal WinAPI dialog.
4. Payload decryption. Derive `key = SHA256(license || kBuildSalt)`, AES-256-CBC-decrypt the embedded `TabTip32_exe[]` array in place.
5. Target selection. `FindRandomTargetProcess` picks a same-subsystem, same-architecture user-level process from `CreateToolhelp32Snapshot`.
6. Process hollowing. `CreateProcessA(target, CREATE_SUSPENDED | CREATE_NO_WINDOW)`. Walks the decrypted payload's headers. `VirtualAllocEx`s the image base. `WriteProcessMemory`s each section. `VirtualProtectEx`s per-section minimal protections. Patches relocations if present. Sets thread context's RIP/EIP to the payload entry point. `ResumeThread`.
7. Retry loop. Up to 8 attempts with different random victims (some random picks can't be hollowed cleanly).
8. Cleanup.

Sanitized PE: post-build script wipes Rich header, timestamp, debug-data directory, randomizes section names, recalculates PE checksum.

### coloruino-app (pipanel.exe)

Runs hollowed inside whatever target the loader picked. From the OS's perspective it appears as e.g. `dllhost.exe` running normally.

Threads:

| Thread | Priority | Pacing | Role |
|---|---|---|---|
| Capture | HIGHEST | DXGI 1ms | Frame grab + LUT classification + per-mode candidate publish |
| mode_a | HIGHEST | edge | Silent aim (one-shot snap on key edge) |
| nonmode_a | HIGHEST | edge | Flicker (one-shot flick, no snap-back) |
| WebServer | normal | accept loop | HTTP config UI on :13548 |
| AntiDebug | normal | 1s tick | Continuous debug detection |

Pipeline per capture iteration:

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

`mode_a` and `nonmode_a` threads watch their respective key edges and fire `SnapShoot_P` (UDP 'P') / `SnapShoot_F` (UDP 'F') as appropriate.

### coloruino-fw (Arduino Leonardo)

Single-threaded Arduino sketch. Runs on power-up.

Boot sequence:

1. Initialize HID Mouse interface (the USB descriptor declares it as the chosen real-mouse identity per the patched AVR core).
2. Initialize Ethernet shield with rotated MAC OUI + the configured IP.
3. Begin UDP listen on port 5353.

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

Commands:

- `M dx dy` - sub-stepped movement with velocity curve and 100-300us inter-step jitter.
- `P dx dy` - silent aim: snap + click + snap-back. UNCHANGED, deterministic.
- `F dx dy` - flicker: snap + click (no snap-back). UNCHANGED, deterministic.
- `L`       - left click hold of randomized duration 20-67 ms.
- `K val`   - set P_COOLDOWN (silent-aim cooldown gate in ms).

---

## End-to-end data flow

A typical aim-assist action from screen pixel to fired shot:

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

Total typical end-to-end latency: 2-5 ms depending on monitor refresh and DXGI delay.

---

## Process hollowing details

The loader uses the classic suspended-process RunPE technique with modern hardening.

### Target selection

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

### Hollowing core (RunPE64 - 64-bit, no relocations)

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

(`RunPEReloc64` is the same with relocations applied at step 6.)

The dynamic WinAPI resolution means `CreateProcessA`, `VirtualAllocEx`, `WriteProcessMemory`, `VirtualProtectEx`, `GetThreadContext`, `SetThreadContext`, `ResumeThread`, and friends are NOT statically imported. Static analysis of `AMDRSHelper.exe` shows zero hollowing-signature imports.

### Retry loop

```
for attempt in 0..8:
  targetPid = FindRandomTargetProcess(subsystem, arch)
  if targetPid == 0: break
  try CreateProcess + RunPE...
  on success: break
  on failure: TerminateProcess(suspended), continue
```

Bounded retries handle the failure modes where a random pick happens to be privileged / job-restricted / EDR-protected / self-terminating.

---

## Network protocol - the DNS-shape

PC application to Arduino UDP packets are crafted to look like DNS queries from a casual inspection of the wire.

### Packet layout (34 bytes total)

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

The 16-byte qname carries a base32 encoding of the 10-byte XOR-encrypted command payload + a 6-byte fixed/checksum tail.

### Command payload (10 bytes, after XOR + base32 decode)

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

`L` (left click) ignores dx/dy. Other commands use them as movement deltas.

### Why this shape

mDNS uses port 5353 and broadcasts DNS-style packets on every local network. Packet captures of the protocol are indistinguishable from typical zeroconf chatter at first glance. Deep packet inspection would reveal the qname is gibberish (base32 of XOR-encrypted bytes) rather than a hostname, but anything sniffing this deep on a private LAN is already past us anyway.

---

## Key derivation chain

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

The license is the ONLY user-supplied secret. Everything else derives from it + build-time random + machine-specific fingerprint. The license never reaches the app at runtime; it only gates the loader.

---

## State at rest

Files that exist on the play PC:

```
AMDRSHelper.exe   signed, packed, sanitized - ~5-15 MB
data              XOR-encrypted, ~1.5 KB - written by the loader
                  on first license entry; rewritten by the app
                  when the user changes settings via WebUI
auth.dat          AES-encrypted (HWID-keyed), 48-64 bytes -
                  written by the loader on first license entry
```

That's it. No registry writes. No services installed. No scheduled tasks. No autorun entries unless you manually add a shortcut to the Startup folder.

Firewall: one inbound rule auto-created on first launch named "AMD Radeon Software Helper" for TCP 13548 (the WebUI port).

`config_generator.exe` is not part of the released deployment - the loader's `data_writer` module writes `data` itself.

---

## State in memory

A snapshot of any RUNNING coloruino-app:

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

A snapshot of the loader (AMDRSHelper.exe) shortly after hollowing succeeds: usually exits within milliseconds of starting the target process. The hollowed process inherits no parent-child relationship beyond the brief suspended-process spawn.

---

## What is NOT in this architecture

For completeness, common features other cheats have that this doesn't:

- No DLL injection. Pure process hollowing only.
- No driver / kernel component.
- No game-process memory reads/writes.
- No game-API hooking.
- No screen overlay.
- No external networking (only LAN UDP to Arduino + LAN TCP for WebUI).
- No telemetry / phone-home.
- No auto-update.
- No anti-VM (deliberately - VMs are also legit dev/test environments).

The defense philosophy is to look as much like a normal background helper process as possible, doing as little observable as possible, with all detection-relevant input arriving via hardware HID rather than software.
