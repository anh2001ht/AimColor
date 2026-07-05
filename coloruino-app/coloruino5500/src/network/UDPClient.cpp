#include "UDPClient.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

static SOCKET udpSock = INVALID_SOCKET;
static sockaddr_in udpAddr;
static bool wsaInitialized = false;

std::string current_arduino_ip;
int current_arduino_port;

// ── Network protocol: DNS-shaped + XOR-encrypted binary ─────────────────
//
// Wire layout (must match coloruino-fw.ino exactly):
// 12B DNS header (txnid + flags + qdcount=1 + zeros)
// 18B QNAME ("\x0a<10char>\x05local\x00")
// 2B QTYPE (0x000C = PTR)
// 2B QCLASS (0x0001 = IN)
// = 34B total
//
// The 10-char QNAME label is base32 of the 6-byte ciphertext:
// payload[0] = command type ('M' 'L' 'P' 'F' 'K')
// payload[1-2] = int16 x (little-endian) - for K, x = cooldown_ms
// payload[3-4] = int16 y (little-endian) - ignored for L/K
// payload[5] = CRC-8 (poly 0x07) over bytes 0-4
//
// Ciphertext = payload XOR'd with kProtoKey rotated by the low nibble of
// the DNS transaction id (which is randomized per packet, so the same
// command never produces the same ciphertext twice).

namespace {

// PLACEHOLDER 16-byte protocol XOR key. MUST match BYTE-FOR-BYTE the
// kProtoKey[] in coloruino-fw/coloruino-fw.ino. Rotate via
// rotate_secrets.py and rebuild + reflash both sides on change.
constexpr uint8_t kProtoKey[16] = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

uint16_t rng16() {
 static std::mt19937 gen{ std::random_device{}() };
 static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFF);
 return static_cast<uint16_t>(dist(gen));
}

uint8_t crc8(const uint8_t* data, size_t len) {
 uint8_t crc = 0;
 for (size_t i = 0; i < len; ++i) {
 crc ^= data[i];
 for (int b = 0; b < 8; ++b) {
 crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
 : static_cast<uint8_t>( crc << 1);
 }
 }
 return crc;
}

void base32_encode_6(const uint8_t in[6], char out[10]) {
 static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
 uint64_t bits = 0;
 uint8_t nbits = 0;
 size_t o = 0;
 for (uint8_t i = 0; i < 6; ++i) {
 bits = (bits << 8) | in[i];
 nbits += 8;
 while (nbits >= 5) {
 nbits -= 5;
 out[o++] = kAlphabet[(bits >> nbits) & 0x1F];
 }
 }
 if (nbits > 0) {
 out[o++] = kAlphabet[(bits << (5 - nbits)) & 0x1F];
 }
}

void send_binary_cmd(char type, int16_t x, int16_t y) {
 if (udpSock == INVALID_SOCKET) return;

 // 1. Build 6-byte plaintext payload.
 uint8_t payload[6];
 payload[0] = static_cast<uint8_t>(type);
 payload[1] = static_cast<uint8_t>(x & 0xFF);
 payload[2] = static_cast<uint8_t>((x >> 8) & 0xFF);
 payload[3] = static_cast<uint8_t>(y & 0xFF);
 payload[4] = static_cast<uint8_t>((y >> 8) & 0xFF);
 payload[5] = crc8(payload, 5);

 // 2. XOR-encrypt with rotating key.
 const uint16_t txnid = rng16();
 const uint8_t nonce = static_cast<uint8_t>(txnid & 0x0F);
 for (uint8_t i = 0; i < 6; ++i) {
 payload[i] ^= kProtoKey[(nonce + i) & 0x0F];
 }

 // 3. Base32-encode the ciphertext into a 10-char label.
 char label[10];
 base32_encode_6(payload, label);

 // 4. Build the DNS query packet.
 uint8_t packet[34];

 // Header
 packet[0] = static_cast<uint8_t>((txnid >> 8) & 0xFF); // txnid HI
 packet[1] = static_cast<uint8_t>( txnid & 0xFF); // txnid LO
 packet[2] = 0x01; // flags HI: QR=0, opcode=0, AA=0, TC=0, RD=1
 packet[3] = 0x00; // flags LO
 packet[4] = 0x00; packet[5] = 0x01; // qdcount = 1
 packet[6] = 0x00; packet[7] = 0x00; // ancount = 0
 packet[8] = 0x00; packet[9] = 0x00; // nscount = 0
 packet[10] = 0x00; packet[11] = 0x00; // arcount = 0

 // Question: QNAME = "\x0a<10char>\x05local\x00"
 packet[12] = 10;
 std::memcpy(&packet[13], label, 10);
 packet[23] = 5;
 packet[24] = 'l';
 packet[25] = 'o';
 packet[26] = 'c';
 packet[27] = 'a';
 packet[28] = 'l';
 packet[29] = 0x00; // null terminator

 // QTYPE = PTR (0x000C), QCLASS = IN (0x0001)
 packet[30] = 0x00; packet[31] = 0x0C;
 packet[32] = 0x00; packet[33] = 0x01;

 sendto(udpSock, reinterpret_cast<const char*>(packet), 34, 0,
 reinterpret_cast<sockaddr*>(&udpAddr), sizeof(udpAddr));
}

} // namespace

bool ensureWinsock() {
 if (wsaInitialized) return true;
 WSADATA d;
 if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
 wsaInitialized = true;
 return true;
}

bool initializeUDPSocket(const char* ip, int port) {
 current_arduino_ip = ip;
 current_arduino_port = port;
 if (!ensureWinsock()) return false;

 udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 if (udpSock == INVALID_SOCKET) return false;

 u_long mode = 1;
 ioctlsocket(udpSock, FIONBIO, &mode);

 memset(&udpAddr, 0, sizeof(udpAddr));
 udpAddr.sin_family = AF_INET;
 udpAddr.sin_port = htons(static_cast<u_short>(port));
 if (inet_pton(AF_INET, ip, &udpAddr.sin_addr) <= 0) {
 closesocket(udpSock); udpSock = INVALID_SOCKET; return false;
 }
 sendCommand(30, 30, 'M');
 return true;
}

bool reconnectSocket() {
 if (udpSock != INVALID_SOCKET) { closesocket(udpSock); udpSock = INVALID_SOCKET; }
 std::this_thread::sleep_for(std::chrono::milliseconds(500));
 return initializeUDPSocket(current_arduino_ip.c_str(), current_arduino_port);
}

void cleanupSocket() {
 if (udpSock != INVALID_SOCKET) { closesocket(udpSock); udpSock = INVALID_SOCKET; }
 if (wsaInitialized) { WSACleanup(); wsaInitialized = false; }
}

void sendCommand(int xx, int yy, char prefix) {
 send_binary_cmd(prefix, static_cast<int16_t>(xx), static_cast<int16_t>(yy));
}

void sendClick() {
 send_binary_cmd('L', 0, 0);
}

void sendArduinoConfig(char cmd, int value) {
 send_binary_cmd(cmd, static_cast<int16_t>(value), 0);
}
