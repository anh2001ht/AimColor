#pragma once

#include <string>

bool ensureWinsock();
bool initializeUDPSocket(const char* ip, int port);
void cleanupSocket();
void sendCommand(int xx, int yy, char prefix);
void sendClick();
bool reconnectSocket();

// Send single-value config commands to Arduino (e.g. 'D' for split delay, 'K' for cooldown)
void sendArduinoConfig(char cmd, int value);

extern std::string current_arduino_ip;
extern int current_arduino_port;
