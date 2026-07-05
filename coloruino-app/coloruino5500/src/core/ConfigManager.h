#pragma once

#include <string>

class ConfigManager {
private:
 static std::string encryptDecrypt(const std::string& data);

public:
 static bool saveConfig(const std::string& ip, int port);
 static bool saveConfig();
 static bool loadConfig();
 static bool loadIPAndPort(std::string& ip, int& port);
 static std::string getLicenseFromConfig();

 friend class LicenseManager;
};
