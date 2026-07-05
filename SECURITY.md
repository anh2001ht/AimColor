# Coloruino - Security & Threat Model

What this system defends against, what it doesn't, and why.

> Vanguard is closed-source. Everything below is best-guess derived from
> public reverse engineering writeups, the UnKnoWnCheaTs article on Raw
> Input statistical analysis, and the mouse-polling-monitor tool's
> source. Treat it as a working model, not a guarantee.

---

## READ THIS FIRST - Mode risk ranking

Vanguard runs statistical analysis on your mouse-input stream over a
rolling window (community testing puts it around **100 events**).
Programmatic patterns - concentrated burst-fire, identical movement
profiles, missing micro-jitter during clicks - show up as anomalies
against a "human moving the mouse" baseline EVEN IF the individual
packets look fine.

| Mode | Risk | Why |
|------|------|-----|
| Aimbot (`apply_delta`) | **LOW** | Continuous smooth tracking, blends into normal mouse movement, sub-stepped with timing jitter at firmware level. |
| Triggerbot (`trigger_action`) | **MEDIUM** | Single click aligned with a deliberate trigger key. Looks like a fast manual click. The uniform 20-67 ms hold is a recognisable distribution shape over many samples but individual clicks pass. |
| Silent aim (`mode_a`) | **HIGH** | Four HID reports back-to-back: snap + click + release + snap-back. Deterministic shape. Concentrated <5ms burst. Vanguard's rolling-window analyzer DOES catch this pattern over enough samples. **GOT ME BANNED MULTIPLE TIMES.** |
| Flickbot (`nonmode_a`) | **HIGH** | Three HID reports back-to-back: snap + click + release. Same shape and risk as silent. |

**Why silent and flickbot are the loudest:**

- They fire `Mouse.report()` 3-4 times in a row with zero jitter between them.
- The movement vectors are deterministic (no human variance).
- They generate a tight cluster of mouse events that looks nothing like organic human flicks at the statistical level.
- They are NOT humanized in the firmware on purpose - adding latency breaks the snap feel.
- Vanguard's 100-event rolling analysis WILL pick up the cluster shape over enough samples.

**If you want longevity:**

- **Cap silent's cooldown** to something realistic (the `K` command sets `P_COOLDOWN`; 50 ms is the default but 200-500 ms in real play is safer).
- **Don't use silent or flick every engagement.** Mix in raw aim, miss shots, real flicks of your own.
- **Don't get cocky after a streak of obvious one-tap kills.** Pattern density across a match matters as much as individual events.
- A single match of obvious silent-aim kills can get you flagged even if everything else is clean.
- **If you're not willing to deliberately throttle yourself, leave silent and flickbot OFF and use aimbot + triggerbot only.**

This is the bit that got me banned. Twice. Take it seriously.

---

## Threat model

### Deployment context

The default deployment is **1PC**: Vanguard, Valorant, the loader, the
hollowed PC application, and the Arduino's USB endpoint all live on
the same box. There is no physical isolation between cheat and
anti-cheat. The defense is **defense-in-depth on one machine**.

The optional 2PC hardening (capture card + separate cheat PC) moves
the cheat application off the GAME PC entirely. In that layout
Vanguard's process scans, module scans, listening-socket enumeration,
and behaviour monitoring on the GAME PC come up empty. The threat
model below covers 1PC; 2PC is the same minus everything Vanguard can
see on the cheat side (no loader process, no WebUI port, no UDP
sender) because none of it runs on the GAME PC at all.

### Adversary

- **Riot Vanguard** - kernel anti-cheat. Runs as a Windows driver
 (`vgk.sys`) loaded at boot, plus a user-mode service (`vgc`). Active
 on the client PC. Has full ring-0 visibility into everything on
 that box.
- **Windows Defender / SmartScreen** - passive scanners on the client PC.
- **A human observer** glancing at the client's screen, Task Manager,
 Properties dialog, or netstat - typically the client themselves.

### Adversary capabilities (assumed)

Vanguard can on the client PC:
- Enumerate every running process.
- Inspect any module loaded in any process.
- Read process memory pages (including the hollowed payload's).
- Hook syscalls + observe NT calls.
- Inspect USB device descriptors of plugged-in devices (including the
 Arduino).
- Capture and statistically analyze RawInput timing on every connected
 HID device.
- Pattern-match known-bad signatures (DLL hashes, function prologues,
 string lookups, code section CRCs) against any process on the box.
- Compare process integrity against signed expectation (for whitelisted
 processes - though most random hollow targets aren't whitelisted).
- Inspect local sockets, listening ports, and outbound LAN UDP.
- Issue HWID bans across Riot accounts on this hardware.

Vanguard probably CANNOT (or hasn't been observed to):
- Reach across Ethernet to inspect the Arduino's firmware or flash
 contents.
- Modify devices that don't expose a write-capable HID/CDC interface.
- Read or capture data the cheat *briefly held in RAM and zeroed*
 (e.g. the decrypted license, the decrypted payload during the hollow
 hand-off - both are wiped within milliseconds).
- Decrypt the encrypted payload inside `AMDRSHelper.exe` on disk
 without first stealing the license + the build salt baked into the
 loader.
- Tell the difference between a real real gaming mouse you mimic and our
 USB descriptor mimic, AT THE DESCRIPTOR LEVEL. (The full HID report
 descriptor bytes may differ; see "Detection vectors we don't address"
 below.)

### Assets

- The license key (high secrecy - gates the build chain).
- The build-time AES salt + IV (medium - recoverable by reversing the
 loader, but rotating per build limits blast radius).
- The PC application image (high - contains every detectable
 signature; never lands on disk in plaintext).
- The user's Riot account (highest - ban is the actual cost).

---

## Defense map (per binary)

### coloruino-loader (AMDRSHelper.exe)

| Defense | What it does | What it doesn't |
|---|---|---|
| Sanitize_pe | Wipes Rich header + timestamp + debug dir, randomizes section names, recalc'd checksum | Doesn't change the import table - `kernel32` / `ntdll` imports still tell you it's a Windows binary |
| Dynamic WinAPI | All hollowing-related WinAPI calls (`CreateProcess`, `VirtualAllocEx`, etc.) resolved via `GetProcAddress` of xorstr-obfuscated names | Doesn't hide LoadLibrary itself, doesn't hide the fact that GetProcAddress was called many times |
| xorstr literals | Every string used (registry paths, function names, file paths) is XOR-encrypted at rest, decrypted lazily | Once decrypted in memory the string is plaintext until SecureZeroMemory'd |
| AES-256-CBC payload | Embedded PC application bytes are AES-encrypted with key derived from user license + per-build random | Doesn't help once the loader is running - payload IS plaintext in memory during hollow |
| TLS callback anti-debug | DLL-load-time hook triggers if a debugger is attached | Trivially bypassed by a kernel-mode anti-anti-debug |
| Multi-thread watchdog | Periodic checks (BeingDebugged, IsDebuggerPresent, hardware breakpoints, NtGlobalFlag, Heap Flags, DebugPort/DebugObjectHandle) | Detects only common user-mode debuggers (x64dbg, Cheat Engine, OllyDbg). Kernel debug + VBox-style instrumentation invisible. |
| Random target | Picks a different victim process each launch | The presence of the loader process itself is still observable for the brief window it runs |
| 8-attempt retry | If a random pick fails to hollow cleanly, picks another | Doesn't help if EVERY process the loader can see has EDR shielding |
| Self-signed Authenticode | Properties dialog reads as a verified AMD publisher (on target machines with root cert installed) | Doesn't bypass Defender's behavioral heuristics - signed doesn't equal safe |
| AMD masquerade | Binary metadata (FileDescription, ProductName, CompanyName) all read as AMD Radeon Software | The signing cert subject also reads as AMD - but it's self-signed; an analyst checking the chain sees "issued by self" |

### coloruino-app (pipanel.exe)

| Defense | What it does | What it doesn't |
|---|---|---|
| Process hollowing | Code runs inside some other process's PID, not its own | Vanguard sees the hollowed process behaving anomalously (memory layout, RWX sections, thread anomalies) - sophisticated analysis would catch this |
| /MT static CRT | No CRT DLL dependency - fewer DLLs loaded than a typical app | Doesn't hide the fact that the static CRT functions are in the image |
| No PDB / no debug info | Stripped symbols | Function prologues + control flow still recoverable via IDA / Ghidra |
| Windows subsystem (no console) | No console window flashes | If Vanguard enumerates subsystems it sees a Windows app, which is the most common kind |
| HWID-bound license check | `data` file is encrypted to current machine's HWID; mismatch = silent exit | Doesn't help once `data` is correctly generated - the binary still runs the cheat |
| Anti-debug copy | Same multi-thread watchdog as loader | Same limitations |
| Defense-in-depth license check | Reads `data`, validates HWID hash, silent exit on mismatch | Bypassable by anyone with debug access |
| WebUI behind Basic auth | Phone control panel requires user/pass | Only a credential stuffing barrier - credentials are baked into the binary |
| WebUI port 13548 | Above well-known range, doesn't conflict with IIS/Apache | A port scan within LAN finds it instantly. Vanguard can also see local listeners. |
| DNS-shape UDP | Outbound packets to Arduino look like mDNS at a glance | Deep packet inspection reveals the qname is encrypted gibberish |
| xorstr literals | Same as loader | Same |

### coloruino-fw (Leonardo firmware)

| Defense | What it does | What it doesn't |
|---|---|---|
| USB descriptor mimicry | Reports VID 0x???? / PID 0x???? (spoofed gaming mouse identity, set per-build) - looks like a real gaming mouse | A USB packet capture of the device's enumeration also shows the placeholder manufacturer + product strings, which match - but a placeholder vendor has a specific manufacturer-defined HID report that we may or may not replicate exactly |
| MAC OUI rotation | Ethernet shield uses one of 6 vendor OUIs (Intel, Realtek, Apple, Samsung, Microsoft, Dell), rotated per boot | Only the OUI varies; the device-specific 3-byte tail is fixed |
| Velocity-curve sub-stepping | M command movement is split across multiple USB polls with ease-in/ease-out timing | At the host's RawInput level, sub-steps collapse to one report per USB poll (~1 ms); the timing variation is internal, not visible |
| Bimodal jitter | 94% 150-405 µs / 6% 1000-1255 µs inter-step delays | Same - internal Arduino timing, not host-visible |
| L command randomization | Click hold time uniform 20-67 ms | A statistical analyzer with many click samples could identify the uniform distribution shape |
| P / F handlers UNCHANGED | Silent aim and flicker fire as-is, no humanization | Deliberate - adding humanization to P/F would degrade the aim feel |
| HID interface order forced | Mouse interface registered before keyboard (matches real (your spoofed) descriptor order) | A device that has only a mouse but enumerates two interfaces is itself a fingerprint - fixed but inherent |

---

## What Vanguard sees on the play PC (1PC mode)

Since everything lives on one box in the default 1PC layout, Vanguard
sees all of the below. In the optional 2PC layout, items 3-9 below
disappear from the GAME PC (they all live on the CHEAT PC instead),
leaving only items 1-2 visible to Vanguard.

1. **A USB device** claiming to be a real gaming mouse you mimic.
 VID/PID, manufacturer string, product string, device descriptor all
 consistent. The HID *report* descriptor bytes are the stock Arduino
 Leonardo descriptor, NOT the real placeholder vendor's - this is a
 fingerprintable mismatch if Vanguard byte-compares against a
 known-good reference.
2. **RawInput events at 1000 Hz** (declared poll rate) with sub-
 millisecond stddev jitter. **THIS IS THE MAIN EXPOSURE** - see the
 mouse-polling-monitor section below. Vanguard sees this stream from
 the perspective of the game process.
3. **A signed binary `AMDRSHelper.exe`** sitting on disk with publisher
 "AMD Radeon Software". Self-signed - chain doesn't validate against
 Microsoft's CA store, but DOES validate locally if the root cert
 was installed during setup. Vanguard knows about legitimate AMD
 publishers and can compare; ours wouldn't match.
4. **A loader process** (briefly) named `AMDRSHelper.exe` that decrypts
 ~5-15 MB of embedded data and immediately does a process-hollowing
 sequence on some randomly-chosen victim. The pattern
 `CreateProcess(SUSPENDED) -> VirtualAllocEx -> WriteProcessMemory
 sections -> SetThreadContext -> ResumeThread` is well-known to all
 modern EDR products and almost certainly to Vanguard.
5. **The hollowed target process** (whatever the loader picked - e.g.
 `dllhost.exe`) with anomalous memory layout: section protections
 that don't match the on-disk image, a thread that didn't come from
 the original PE's entry point, no symbol resolution for the
 "loaded" image. Kernel scan of this would flag.
6. **A listening TCP port 13548** with firewall rule "AMD Radeon
 Software Helper". Visible via local socket enumeration.
7. **Outbound UDP traffic** to whatever IP the Arduino's Ethernet
 shield has (default `192.168.1.216:5353`). Looks like mDNS at first
 glance; deep packet inspection reveals encrypted qname gibberish.
8. **An Ethernet adapter** on the client PC with a static IP on the
 same subnet as the Arduino's shield. Unusual unless the client has
 a legitimate reason for it (which they might - multi-NIC PCs are
 common).
9. **No autorun entries**, no installed services, no registry trail.
 `AMDRSHelper.exe` launches only when the user (or a manually-added
 shortcut in Startup) starts it.

---

## The mouse polling monitor risk

The C# WPF tool at `extra/mouse-polling-monitor/` reverse engineers
likely Vanguard heuristics:

```
verdict = INJECTION if stddev ≤ 0.015 ms OR burst-rate ≥ 20%
verdict = SUSPICIOUS if stddev ≤ 0.060 ms OR burst-rate ≥ 10%
verdict = NATURAL otherwise
 burst = consecutive movement packets <0.5 ms apart
 window = last 100 movement packets
 min samples = 30
```

Our current firmware:
- Burst rate: **0%** - USB poll caps at 1 ms, no bursts at the host
 level. ✓
- StdDev: **10-50 µs** - only USB scheduling jitter, since our internal
 Arduino sub-step jitter (150-405 µs) gets coalesced by the USB layer.
 ⚠ borderline-to-flagged.

The burst-rate vector we pass cleanly. The stddev vector is the
exposure: on a clean machine with no other USB traffic, our stddev
could dip into INJECTION range (≤15 µs). On a busy machine it floats
into NATURAL range.

**Mitigation (deferred, requires user OK)**: Phase 3.1 firmware tweak.
Inside M command handler, vary how many USB poll boundaries we wait
between sub-step emissions. 80% emit at next poll, 15% skip 1, 5% skip
2-4. Produces ~1.3 ms mean, ~0.4-0.6 ms stddev - well above all
thresholds.

Whether this is needed: run the polling monitor against your live
firmware first. If the verdict shows NATURAL, leave firmware alone.

---

## Detection vectors we don't address

These are areas where Vanguard could catch coloruino that we don't try
to hide:

### Statistical movement-pattern analysis

Real human aim flicks have characteristic acceleration profiles. The
hand accelerates, decelerates, micro-corrects. Our M command sub-steps
follow a velocity curve but it's the same curve every time. A
classifier with enough samples could distinguish.

### Cross-correlation between mouse and clicks

Humans micro-jitter mouse position while pressing the mouse button.
Coloruino's silent aim handler doesn't - the click is a clean burst
with no jitter on the held button. Statistical correlation analysis
between mouse and click streams could flag this.

### Session-level pattern shifts

When the cheat fires, the timing distribution of mouse events shifts
abruptly. A classifier looking at the *transition* between idle and
aim-burst could detect the discontinuity even if both states
individually look human.

### USB descriptor full-fidelity comparison

Vanguard could fetch the HID report descriptor and compare byte-for-byte
to a known-good placeholder vendor descriptor. We mimic the device descriptor
but the HID report descriptor is the stock Arduino Leonardo Mouse
descriptor - not the real placeholder vendor's. Mismatch.

### Memory page protections

Process hollowing produces specific anomalous patterns: a process
created suspended, sections written before resume, EIP set externally.
Recent EDR products (and likely Vanguard) detect this directly via
kernel callbacks on `NtProtectVirtualMemory` and `NtSetContextThread`.

### Behavior under load

Coloruino's CPU usage is small but characteristic - DXGI calls, large
LUT scans, periodic UDP sendto. A behavior profile would identify it.

---

## Secrets lifecycle

### License key

- **Source**: chosen by you at build time, embedded as FNV-1a hash in
 loader / app / config-gen.
- **Where it appears**: user types it once into config-gen, once into
 loader dialog (or once into auth.dat).
- **Rotation**: every release. Recompile everything that bakes the hash.
- **Compromise impact**: someone could redeem the license on their own
 machine if they have the binaries. HWID binding limits but doesn't
 prevent.

### Build salt (kBuildSalt, 32 bytes)

- **Source**: `secrets.token_bytes(32)` per build via gen_build_secrets.py.
- **Where it appears**: only in the loader binary, only as build_secrets.h
 at compile time.
- **Rotation**: every build (automatic - vcxproj PreBuildEvent).
- **Compromise impact**: someone reversing the loader could derive
 the payload AES key with the license. Trivial - but only matters if
 the license is also leaked.

### Payload IV (kPayloadIV, 16 bytes)

- **Source**: same as kBuildSalt.
- **Where**: build_secrets.h.
- **Rotation**: every build.
- **Compromise impact**: alone, nothing - IV isn't a secret. Needs the
 key to decrypt anything.

### HWID salt (kHwidSalt, 12 bytes)

- **Source**: hardcoded constant matching the config XOR key.
- **Where**: loader `hwid.cpp`, app `LicenseManager.cpp`, config-gen.
- **Rotation**: via rotate_secrets.py (alongside config XOR key).
- **Compromise impact**: lets attacker rebuild HWID hash from
 raw machine info - could potentially forge `auth.dat` / `data`. Mid-impact.

### WebUI credentials

- **Source**: rotate_secrets.py picks random pair.
- **Where**: app Auth.cpp.
- **Rotation**: per release.
- **Compromise impact**: LAN access only - already requires being on the
 user's home network. Phone bookmark + bookmark file might leak both.
 Acceptable risk.

### Protocol XOR key (16 bytes)

- **Source**: rotate_secrets.py.
- **Where**: app UDPClient.cpp, firmware coloruino-fw.ino.
- **Rotation**: per release. Both PC and firmware must rebuild +
 redeploy.
- **Compromise impact**: someone capturing LAN UDP could decrypt the
 payload format. Already on the LAN, already low concern.

### Signing private key (code_signing.pfx + .password.txt)

- **Source**: 01_generate_cert.ps1 once.
- **Where**: build machine only. Gitignored.
- **Rotation**: when you suspect compromise OR every few years.
- **Compromise impact**: attacker could sign their own binaries that
 read as "AMD Radeon Software" on target machines that trust the
 cert. Significant - but the cert is also self-issued, so it only
 works on machines you control.

---

## What to rotate, when

| Trigger | Rotate |
|---|---|
| New game patch | nothing automatically - wait and see if detection vectors change |
| Suspected detection | license, payload AES, signing identity (everything) |
| Sharing the binary with a new user | license + WebUI creds (so they get their own access scope) |
| Periodic hygiene (monthly?) | license + WebUI creds |
| Build machine compromise | signing identity (.pfx) |
| `data` or `auth.dat` corruption (no compromise suspected) | nothing - just regenerate the affected file |

---

## What an investigator would see

If a competent forensic analyst gets the binaries:

1. `AMDRSHelper.exe` looks like an AMD Radeon binary signed by a
 suspect publisher. The signing cert is self-signed (not chained
 to a real CA) - first red flag.
2. Static analysis of the loader shows:
 - Heavy use of `GetProcAddress` to resolve runtime APIs (red flag - 
 legitimate AMD binaries link these statically).
 - An embedded ~5 MB encrypted blob (red flag - looks like a packed
 payload).
 - VMProtect markers in the code (if VMProtect is used) or extensive
 anti-debug routines (also red flag).
3. The binary has no Rich header, randomized section names, zero
 TimeDateStamp - all consistent with intentional obfuscation.
4. Dynamic analysis: running it under a debugger triggers the anti-debug,
 which detects the debugger and exits. Without a debugger, it succeeds.

A motivated analyst will figure out it's a cheat within a few hours.
Our defense is volume: most observers (Riot triage analysts, automated
classifiers, AV scanners) don't have hours per sample. We aim to be
boring enough at first glance to not warrant deep dive.

---

## Failure modes (what gets us caught, in order of probability)

1. **Vanguard updates** with a new heuristic targeting our specific
 technique (process hollowing pattern, DXGI sustained capture,
 etc.). Mitigation: rotate to new technique.
2. **HWID ban** from a previous infraction on the same machine. No
 defense - change motherboards or stop playing on that account.
3. **Statistical mouse pattern detection** at the level the
 mouse-polling-monitor tool simulates. Mitigation: Phase 3.1 firmware
 tweak.
4. **Sharing binaries that leak** - friend gets banned, their crash
 dump / file submission reaches Riot, Riot's analysis pipeline
 identifies your build. Mitigation: don't share, or rotate aggressively
 if you do.
5. **Defender / cloud AV** flagging the loader as suspicious based on
 entropy/behavior. Mitigation: signing helps; Defender exclusions
 help more.
6. **You make a mistake** - left a debugger attached during a match,
 left a console window open, used the WebUI from a friend's phone
 that's logged into your Riot account, etc.

---

## Out-of-scope, conscious choices

- No VM/sandbox detection. We don't avoid VMs because VMs are also where
 legit development and testing happen.
- No timing-attack-resistant license comparison. The license check uses
 FNV-1a which isn't constant-time. Anyone running the loader under
 instrumentation can side-channel the license. Accepting this - the
 loader being side-channelable doesn't help an external Vanguard.
- No protection against memory scrapers running on the same machine.
 If something has the privilege to read our process memory, they have
 it all. We focus on the network-and-USB-isolated threat model.
- No anti-WoW64. We're 64-bit only; if someone runs us under WoW64
 they're trying to debug us and we'd rather just fail.

---

## If you find a real bug or detection

1. Don't keep playing on the affected account.
2. Capture: which build (rotate_secrets timestamp), which Windows
 version, which Vanguard version (from `vgc` service description),
 which target process the loader picked.
3. Document the specific symptom: ban email text, specific Vanguard
 error code, etc.
4. Rotate everything before next release.
5. Consider whether the failure points to a single specific defense
 that needs replacing - versus general detection that means the
 technique is burned.
