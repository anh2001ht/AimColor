#pragma once

#include <string>

class LicenseManager {
private:
 static std::string generateHWID();
 static std::string hashHWID(const std::string& hwid);
 static std::string getLicenseFromConfig();
 static bool saveLicenseToConfig(const std::string& hashedHWID);
 static std::string getHiddenInput();
 static bool promptForLicense();

public:
 static bool isLicensed();
 static bool validateAndBindLicense(const std::string& licenseKey);
 static bool checkLicense();
};
