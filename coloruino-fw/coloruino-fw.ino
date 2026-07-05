#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <usbhub.h>
#include <hiduniversal.h>
#include "hidcustom.h"
#include "SecondHIDIface.h" // Interface 1 - keyboard/consumer/vendor clone

// ── Device identity ─────────────────────────────────────────────────────────
// These should ALSO be set by boards.txt via -DUSB_VID / -DUSB_PID / etc.,
// and SHOULD match the real device you are mimicking (VID, PID, manufacturer
// string, product string, device descriptor layout).
//
// PLACEHOLDERS BELOW. You MUST pick a real gaming-mouse identity to mimic
// (capture from your own physical mouse with Wireshark/USBPcap or the
// Windows USB device viewer) and patch your local Arduino AVR core's
// USBCore.cpp + USBDesc.h to match. Shipping a 0x0000/0x0000 VID/PID is
// non-functional as a disguise it just stops the binary from claiming
// to be a specific real product.
#ifndef USB_VID
#define USB_VID 0x0000 // PLACEHOLDER
#endif
#ifndef USB_PID
#define USB_PID 0x0000 // PLACEHOLDER
#endif
#ifndef USB_MANUFACTURER
#define USB_MANUFACTURER "PLACEHOLDER_MFR"
#endif
#ifndef USB_PRODUCT
#define USB_PRODUCT "PLACEHOLDER_PRODUCT"
#endif

// ── Register Interface 1 (consumer/keyboard/vendor) ─────────────────────────
// Force HID() singleton construction BEFORE SecondHIDIface the reference
// initialization touches HID(), causing its PluggableUSB().plug() to run
// first. Otherwise SecondHIDIface_'s constructor would steal Interface 0.
// Required so the descriptor layout matches the real device you mimic:
// Interface 0 = HID Mouse Boot Protocol (66-byte report desc)
// Interface 1 = HID Composite Keyboard (140-byte report desc)
static auto& _force_hid_first = HID();
SecondHIDIface_ SecondHIDIface;

// ── MAC address config ──────────────────────────────────────────────────
// true = generate random MAC each boot using a randomly-picked legit OUI
// false = use the hard-coded MAC below
// The MAC pool below contains real OUIs from common consumer-network
// vendors. Each boot picks one at random, so the LAN-visible device
// looks like a different "PC NIC / phone / console" every session
// instead of having a stable fingerprint.
#define MAC_RANDOM true
byte mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // placeholder fallback

// Six real-world OUIs sampled from common consumer-LAN vendors. No two
// homes have an identical device mix, so rotating among these blends
// into typical ARP-table contents.
static const uint8_t legitOUIs[6][3] = {
 { 0x00, 0x1B, 0x21 }, // Intel Corporate (PC NIC)
 { 0x00, 0xE0, 0x4C }, // Realtek Semiconductor (PC NIC / motherboard)
 { 0x3C, 0x22, 0xFB }, // Apple (iPhone / iPad / Mac)
 { 0x28, 0x39, 0x5E }, // Samsung Electronics (Galaxy / smart TV)
 { 0xC8, 0x9F, 0x1D }, // Microsoft Corporation (Xbox / Surface)
 { 0x00, 0x14, 0x22 } // Dell Inc. (Dell PC NIC)
};

unsigned long lastPTime = 0;
unsigned long P_COOLDOWN = 50; // milliseconds - configurable via 'K' command
 // Default 50ms = supports 20Hz spam.
 // PC pushes its cfg value on startup, so this
 // is only the pre-handshake fallback.

// ── Network config ──────────────────────────────────────────────────────
// PLACEHOLDER subnet. Set to whatever spare /24 you wire the W5500
// into on your PC. Default Arduino IP, gateway, and subnet below assume
// a dedicated point-to-point Ethernet between PC and Arduino.
IPAddress ip(192, 168, 1, 216);
IPAddress dns_server(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

EthernetUDP Udp;
// Port 5353 = standard mDNS. Combined with the DNS-shaped wire format
// below, traffic looks like mDNS PTR queries to a passive sniffer.
unsigned int localPort = 5353;

// ── UDP receive buffer (128 bytes - enough for long chained commands) ───
#define CMD_BUFFER_SIZE 128
char packetBuffer[CMD_BUFFER_SIZE];

// ── USB Host ────────────────────────────────────────────────────────────
USB Usb;
USBHub Hub(&Usb);
HIDUniversal Hid(&Usb);
MouseRptParser mouseParser;

// ── Helpers ─────────────────────────────────────────────────────────────

void genMAC() {
 randomSeed(analogRead(0) + micros());
 // Pick a real OUI from the consumer-vendor pool - different vendor
 // every boot, so a passive ARP scan never sees the same fingerprint
 // twice in a row. Last three bytes random within that OUI.
 const uint8_t pick = (uint8_t)random((long)(sizeof(legitOUIs) / sizeof(legitOUIs[0])));
 mac[0] = legitOUIs[pick][0];
 mac[1] = legitOUIs[pick][1];
 mac[2] = legitOUIs[pick][2];
 for (uint8_t i = 3; i < 6; i++) {
 mac[i] = random(256);
 }
}

// ── Humanization helpers ────────────────────────────────────────────────
//
// Vanguard's 2PC heuristics (see UC writeup from mutecb, 2026-06) flag two
// signals on aimbot M output: (1) inter-report std dev too low - the device
// queues at exact game-tick boundaries vs a real mouse's ~1 kHz sensor
// cadence - and (2) reports too far apart on average (5 ms tracking vs
// ~1 ms for a real mouse). Sub-stepping each M command into multiple
// velocity-weighted reports with sub-ms jitter brings both signals into
// the human cluster. P (silent) and F (flicker) are intentionally NOT
// humanized - they're rare one-shots and any added latency defeats their
// purpose.

// xorshift32 PRNG - fast, deterministic, non-cryptographic. Seeded in
// setup(). State must be non-zero (the algorithm fixes at zero).
static uint32_t prng_state = 0xDEADBEEF;
uint32_t xorshift32() {
 uint32_t x = prng_state;
 x ^= x << 13;
 x ^= x >> 17;
 x ^= x << 5;
 prng_state = x;
 return x;
}

// Velocity-weighted sub-step distribution (percentage out of 100 per step).
// Row index = steps - 1. Bell-shaped curves approximate the accel → peak →
// decel velocity profile of a natural human flick. Trailing zeros are
// unused (rows for step counts < 8).
static const uint8_t velocity_weights[8][8] = {
 {100, 0, 0, 0, 0, 0, 0, 0},
 { 50, 50, 0, 0, 0, 0, 0, 0},
 { 25, 50, 25, 0, 0, 0, 0, 0},
 { 15, 35, 35, 15, 0, 0, 0, 0},
 { 10, 25, 30, 25, 10, 0, 0, 0},
 { 8, 20, 22, 22, 20, 8, 0, 0},
 { 7, 15, 20, 22, 16, 13, 7, 0},
 { 6, 12, 18, 20, 18, 14, 8, 4}
};

// Pick step count from movement magnitude. Small deltas stay as one
// report - sub-stepping a 2 px move into 4 reports loses sub-pixel
// precision to int truncation and shows up as zero-delta reports.
//
// Phase 6.1 aggressive tuning:
// - Sub-step threshold raised from 3 → 10 px: most tracking
// micro-corrections fire as a single report (lower latency).
// - Max steps capped at 5 (was 8): large flicks complete in ~5 USB polls
// instead of 8, saving ~3 ms wall-clock per flick.
static uint8_t pick_steps(int absX, int absY) {
 int mag = absX > absY ? absX : absY;
 if (mag < 10) return 1;
 if (mag < 30) return 3;
 if (mag < 80) return 4;
 return 5;
}

// Emit a single M-command delta as N velocity-weighted Mouse.report calls
// with 100-300 µs jitter between reports (Phase 6.1 tight uniform).
// Cumulative-percent tracking + per-step diffing avoids drift from rounding
// (final position is exact).
void sub_step_move(uint8_t real, int x, int y) {
 int absX = x < 0 ? -x : x;
 int absY = y < 0 ? -y : y;
 uint8_t steps = pick_steps(absX, absY);

 if (steps <= 1) {
 Mouse.report(real, (int16_t)x, (int16_t)y);
 return;
 }

 const uint8_t* w = velocity_weights[steps - 1];
 int sentX = 0;
 int sentY = 0;
 int cumPct = 0;
 for (uint8_t i = 0; i < steps; i++) {
 if (i > 0) {
 // Phase 6.1 aggressive: tight uniform 100-300 µs only. Dropped
 // the prior bimodal 6%-at-1000-1255-µs slow mode - it added ~60µs
 // mean and an occasional 1 ms pause that the user perceived as
 // sluggish. Trade-off: host-side std dev drops, may push the
 // polling-monitor verdict from NATURAL toward SUSPICIOUS on a
 // clean test rig. Run extra/mouse-polling-monitor to confirm
 // before relying on this in ranked.
 uint32_t r = xorshift32();
 uint16_t delay_us = 100 + (uint16_t)((r >> 4) % 200); // 100-299 µs
 delayMicroseconds(delay_us);
 }
 cumPct += w[i];
 int targetX = (x * cumPct) / 100;
 int targetY = (y * cumPct) / 100;
 int dx = targetX - sentX;
 int dy = targetY - sentY;
 sentX = targetX;
 sentY = targetY;
 Mouse.report(real, (int16_t)dx, (int16_t)dy);
 }
}

// ── Network protocol: DNS-shaped + XOR-encrypted binary ──────────────────
//
// Wire format on the LAN: standard 12-byte DNS header + a single question
// section. First QNAME label is exactly 10 chars (base32 of 6 ciphertext
// bytes), second label is "local", QTYPE=PTR (0x000C), QCLASS=IN (0x0001).
// To a passive sniffer this looks like an ordinary mDNS PTR query.
//
// Inside the 10-char label, after base32-decode and XOR-decrypt:
// byte 0: command type ('M' 'L' 'P' 'F' 'K')
// byte 1-2: int16 x (little-endian) - for K, x carries cooldown_ms
// byte 3-4: int16 y (little-endian) - ignored for L/K
// byte 5: CRC-8 (poly 0x07) over bytes 0-4
//
// XOR key is rotated per packet using the low nibble of the DNS
// transaction id as the starting index - the txnid is randomized by the
// PC sender, so every packet's ciphertext differs even for identical
// payloads.

// PLACEHOLDER 16-byte protocol XOR key. MUST match BYTE-FOR-BYTE the
// kProtoKey[] in coloruino-app/.../UDPClient.cpp. Rotate via
// rotate_secrets.py and reflash + rebuild both sides on change.
static const uint8_t kProtoKey[16] = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int8_t b32_val(char c) {
 if (c >= 'a' && c <= 'z') return (int8_t)(c - 'a');
 if (c >= '2' && c <= '7') return (int8_t)(c - '2' + 26);
 return -1;
}

static bool base32_decode_10(const char* in, uint8_t* out) {
 uint64_t bits = 0;
 uint8_t nbits = 0;
 uint8_t out_idx = 0;
 for (uint8_t i = 0; i < 10; i++) {
 int8_t v = b32_val(in[i]);
 if (v < 0) return false;
 bits = (bits << 5) | (uint64_t)v;
 nbits += 5;
 while (nbits >= 8) {
 nbits -= 8;
 if (out_idx >= 6) return false;
 out[out_idx++] = (uint8_t)((bits >> nbits) & 0xFF);
 }
 }
 return out_idx == 6;
}

static uint8_t crc8(const uint8_t* data, uint8_t len) {
 uint8_t crc = 0;
 for (uint8_t i = 0; i < len; i++) {
 crc ^= data[i];
 for (uint8_t b = 0; b < 8; b++) {
 crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
 }
 }
 return crc;
}

static void exec_cmd(char type, int16_t x, int16_t y) {
 // Preserve real mouse button state from USB Host passthrough.
 uint8_t real = mouseParser.prevButtons;

 switch (type) {
 case 'M':
 sub_step_move(real, x, y);
 break;

 case 'L': {
 Mouse.report(real | MOUSE_LEFT, 0, 0);
 const unsigned long hold_ms = 20 + (xorshift32() % 48);
 const unsigned long start_ms = millis();
 while (millis() - start_ms < hold_ms) Usb.Task();
 Mouse.report(real, 0, 0);
 break;
 }

 case 'P':
 // Silent aim - 4-report deterministic burst, UNCHANGED per rule.
 if (millis() - lastPTime < P_COOLDOWN) return;
 Mouse.report(real, x, y); // 1. MOVE
 Mouse.report(real | MOUSE_LEFT, 0, 0); // 2. PRESS at target
 Mouse.report(real, 0, 0); // 3. RELEASE at target
 Mouse.report(real, -x, -y); // 4. SNAPBACK
 lastPTime = millis();
 break;

 case 'F':
 // Flicker - 3-report burst, UNCHANGED per rule.
 Mouse.report(real, x, y);
 Mouse.report(real | MOUSE_LEFT, 0, 0);
 Mouse.report(real, 0, 0);
 break;

 case 'K':
 // Set P cooldown in ms; PC pushes this on startup.
 P_COOLDOWN = (unsigned long)(uint16_t)x;
 break;
 }
}

// Parse one fake-DNS packet. Strictly validates the question section
// layout we emit; rejects everything else silently (collisions with real
// mDNS PTR queries from other LAN devices fail the layout/CRC checks).
static void handle_dns_packet(const uint8_t* buf, int len) {
 // Header(12) + qname_len(1) + label(10) + sep(1) + "local"(5) + null(1)
 // + qtype(2) + qclass(2) = 34 bytes minimum.
 if (len < 34) return;

 // qdcount must be 1 (one question). Other fields free - passive
 // mDNS queries on the LAN may have varying flags.
 if (buf[4] != 0 || buf[5] != 1) return;

 // First QNAME label length must be exactly 10.
 if (buf[12] != 10) return;

 // Second label must be ".local" (length 5, then "local", then null).
 if (buf[23] != 5
 || buf[24] != 'l' || buf[25] != 'o' || buf[26] != 'c'
 || buf[27] != 'a' || buf[28] != 'l'
 || buf[29] != 0) {
 return;
 }

 // Decode base32 → 6 ciphertext bytes.
 uint8_t payload[6];
 if (!base32_decode_10((const char*)&buf[13], payload)) return;

 // XOR-decrypt with key rotated by low nibble of txnid.
 const uint8_t nonce = buf[1] & 0x0F;
 for (uint8_t i = 0; i < 6; i++) {
 payload[i] ^= kProtoKey[(nonce + i) & 0x0F];
 }

 // Verify CRC over bytes 0..4.
 if (crc8(payload, 5) != payload[5]) return;

 const int16_t x = (int16_t)((uint16_t)payload[1] | ((uint16_t)payload[2] << 8));
 const int16_t y = (int16_t)((uint16_t)payload[3] | ((uint16_t)payload[4] << 8));
 exec_cmd((char)payload[0], x, y);
}

// ── Setup ───────────────────────────────────────────────────────────────

void setup() {
 const int W5500_RST = 7;

 pinMode(W5500_RST, OUTPUT);

 digitalWrite(W5500_RST, LOW);
 delay(100);

 digitalWrite(W5500_RST, HIGH);
 delay(1000);
 Usb.Init();
 Mouse.begin();

 Hid.SetReportParser(0, &mouseParser);

 Ethernet.init(6);
 if (MAC_RANDOM) genMAC();
 Ethernet.begin(mac, ip, dns_server, gateway, subnet);
 Udp.begin(localPort);

 // Seed the xorshift32 PRNG used for sub-step timing jitter. Any
 // non-zero seed is fine; micros() is post-init so has nonzero entropy.
 prng_state = micros() ^ ((uint32_t)analogRead(1) << 16);
 if (prng_state == 0) prng_state = 0xDEADBEEF;
}

// ── Main loop ───────────────────────────────────────────────────────────
// UDP is polled BOTH before and after Usb.Task(). Usb.Task() blocks for
// 100-500 µs per call (real-mouse passthrough); a packet that arrives
// during that window would otherwise sit in the W5500 buffer until the
// next loop iter. Polling on both sides catches it immediately.

static inline void poll_udp_once() {
 int packetSize = Udp.parsePacket();
 if (packetSize >= 34) { // 34 = minimum size of our fake-DNS query packet
 int len = Udp.read(packetBuffer, CMD_BUFFER_SIZE);
 if (len >= 34) {
 handle_dns_packet((const uint8_t*)packetBuffer, len);
 }
 }
}

void loop() {
 poll_udp_once();
 Usb.Task();
 poll_udp_once();
}