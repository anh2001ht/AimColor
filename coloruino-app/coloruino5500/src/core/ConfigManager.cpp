#include "ConfigManager.h"
#include "Config.h"
#include "xorstr.h"
#include "VMProtectSDK.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <Windows.h>

std::string ConfigManager::encryptDecrypt(const std::string& data) {
 VMProtectBeginMutation("eD");
 // PLACEHOLDER 24-hex / 12-byte XOR key. Rotate via rotate_secrets.py.
 // MUST match LicenseManager salt + loader's data_writer.cpp.
 std::string key(xorstr_("000000000000000000000000"));
 std::string result = data;
 const size_t keyLength = key.size();
 for (size_t i = 0; i < result.length(); i++) {
 result[i] = result[i] ^ key[i % keyLength];
 }
 SecureZeroMemory(&key[0], key.size());
 VMProtectEnd();
 return result;
}

bool ConfigManager::saveConfig(const std::string& ip, int port) {
 try {
 std::stringstream configData;

 configData << ip << "\n";
 configData << port << "\n";

 std::string existingLicense = getLicenseFromConfig();
 if (!existingLicense.empty()) {
 configData << xorstr_("LICENSE_HWID=") << existingLicense << "\n";
 }

 configData << xorstr_("---CONFIG_START---") << "\n";

 configData << "apply_delta_ativo=" << (cfg::apply_delta_ativo ? "1" : "0") << "\n";
 configData << "apply_deltakey1=" << cfg::apply_deltakey1 << "\n";
 configData << "apply_deltakey2=" << cfg::apply_deltakey2 << "\n";
 configData << "target_offset_x=" << cfg::target_offset_x << "\n";
 configData << "target_offset_y=" << cfg::target_offset_y << "\n";
 configData << "apply_delta_fov=" << cfg::apply_delta_fov << "\n";
 configData << "apply_delta_smooth=" << cfg::apply_delta_smooth << "\n";
 configData << "speed=" << cfg::speed << "\n";
 configData << "sleep=" << cfg::sleep << "\n";

 configData << "apply_deltaassist_ativo=" << (cfg::apply_deltaassist_ativo ? "1" : "0") << "\n";
 configData << "assist_apply_deltakey=" << cfg::assist_apply_deltakey << "\n";
 configData << "assist_target_offset_x=" << cfg::assist_target_offset_x << "\n";
 configData << "assist_target_offset_y=" << cfg::assist_target_offset_y << "\n";
 configData << "apply_deltaassist_fov=" << cfg::apply_deltaassist_fov << "\n";
 configData << "apply_deltaassist_smooth=" << cfg::apply_deltaassist_smooth << "\n";
 configData << "assist_speed=" << cfg::assist_speed << "\n";
 configData << "assist_sleep=" << cfg::assist_sleep << "\n";

 configData << "nonmode_a_ativo=" << (cfg::nonmode_a_ativo ? "1" : "0") << "\n";
 configData << "nonmode_a_key=" << cfg::nonmode_a_key << "\n";
 configData << "nonmode_a_fov=" << cfg::nonmode_a_fov << "\n";
 configData << "nonmode_a_delay_between_shots=" << cfg::nonmode_a_delay_between_shots << "\n";
 configData << "nonmode_a_distance=" << cfg::nonmode_a_distance << "\n";

 configData << "mode_a_ativo=" << (cfg::mode_a_ativo ? "1" : "0") << "\n";
 configData << "mode_a_key=" << cfg::mode_a_key << "\n";
 configData << "mode_a_target_offset_x=" << cfg::mode_a_target_offset_x << "\n";
 configData << "mode_a_target_offset_y=" << cfg::mode_a_target_offset_y << "\n";
 configData << "mode_a_fov=" << cfg::mode_a_fov << "\n";
 configData << "mode_a_delay_between_shots=" << cfg::mode_a_delay_between_shots << "\n";
 configData << "distance=" << cfg::distance << "\n";
 configData << "mode_a_head_targeting=" << (cfg::mode_a_head_targeting ? "1" : "0") << "\n";
 configData << "mode_a_cooldown_ms=" << cfg::mode_a_cooldown_ms << "\n";

 configData << "trigger_action_ativo=" << (cfg::trigger_action_ativo ? "1" : "0") << "\n";
 configData << "trigger_action_key=" << cfg::trigger_action_key << "\n";
 configData << "trigger_action_delay=" << cfg::trigger_action_delay << "\n";
 configData << "trigger_action_fovX=" << cfg::trigger_action_fovX << "\n";
 configData << "trigger_action_fovY=" << cfg::trigger_action_fovY << "\n";
 configData << "useIstrigFilter=" << (cfg::useIstrigFilter ? "1" : "0") << "\n";

 configData << "color_mode=" << cfg::color_mode << "\n";

 for (int i = 0; i < 3; i++) {
 configData << "menorRGB" << i << "=" << cfg::menorRGB[i] << "\n";
 configData << "maiorRGB" << i << "=" << cfg::maiorRGB[i] << "\n";
 configData << "menorHSV" << i << "=" << cfg::menorHSV[i] << "\n";
 configData << "maiorHSV" << i << "=" << cfg::maiorHSV[i] << "\n";
 }

 configData << "ngrok=" << cfg::ngrok << "\n";
 configData << "use_gpu_processing=" << (cfg::use_gpu_processing ? "1" : "0") << "\n";

 configData << "dead_body_filter=" << (cfg::dead_body_filter ? "1" : "0") << "\n";
 configData << "dead_body_threshold=" << cfg::dead_body_threshold << "\n";
 configData << "min_cluster_size=" << cfg::min_cluster_size << "\n";

 configData << "trigger_polygon_check=" << (cfg::trigger_polygon_check ? "1" : "0") << "\n";
 configData << "apply_delta_dist_smoothing=" << (cfg::apply_delta_dist_smoothing ? "1" : "0") << "\n";
 configData << "apply_delta_near_dist=" << cfg::apply_delta_near_dist << "\n";
 configData << "apply_delta_mid_dist=" << cfg::apply_delta_mid_dist << "\n";
 configData << "apply_delta_near_mult=" << cfg::apply_delta_near_mult << "\n";
 configData << "apply_delta_mid_mult=" << cfg::apply_delta_mid_mult << "\n";

 configData << "head_anchor_proportional=" << (cfg::head_anchor_proportional ? "1" : "0") << "\n";
 configData << "head_anchor_band_rows=" << cfg::head_anchor_band_rows << "\n";
 configData << "head_anchor_gap_tolerance=" << cfg::head_anchor_gap_tolerance << "\n";
 configData << "head_anchor_close_pct=" << cfg::head_anchor_close_pct << "\n";
 configData << "head_anchor_mid_pct=" << cfg::head_anchor_mid_pct << "\n";
 configData << "head_anchor_close_min_h=" << cfg::head_anchor_close_min_h << "\n";
 configData << "head_anchor_mid_min_h=" << cfg::head_anchor_mid_min_h << "\n";

 std::string encryptedData = encryptDecrypt(configData.str());

 std::ofstream outFile(xorstr_("data"), std::ios::binary);
 if (outFile.is_open()) {
 outFile.write(encryptedData.c_str(), encryptedData.size());
 outFile.close();
 return true;
 }
 return false;
 }
 catch (const std::exception&) {
 return false;
 }
}

bool ConfigManager::saveConfig() {
 std::string ip;
 int port;
 if (loadIPAndPort(ip, port)) {
 return saveConfig(ip, port);
 }
 return saveConfig(xorstr_("192.168.1.216"), 5353);  // placeholder defaults
}

bool ConfigManager::loadConfig() {
 try {
 std::ifstream inFile(xorstr_("data"), std::ios::binary);
 if (!inFile.is_open()) return false;

 std::stringstream buffer;
 buffer << inFile.rdbuf();
 inFile.close();

 std::string decryptedData = encryptDecrypt(buffer.str());

 std::istringstream iss(decryptedData);
 std::string line;

 std::getline(iss, line); // IP
 std::getline(iss, line); // Port

 while (std::getline(iss, line)) {
 if (line.find(xorstr_("LICENSE_HWID=")) == 0) continue;
 if (line == xorstr_("---CONFIG_START---")) break;
 }

 while (std::getline(iss, line)) {
 size_t pos = line.find('=');
 if (pos == std::string::npos) continue;

 std::string key = line.substr(0, pos);
 std::string valueStr = line.substr(pos + 1);

 if (key == "apply_delta_ativo") cfg::apply_delta_ativo = (valueStr == "1");
 else if (key == "apply_deltakey1") cfg::apply_deltakey1 = std::stoi(valueStr);
 else if (key == "apply_deltakey2") cfg::apply_deltakey2 = std::stoi(valueStr);
 else if (key == "target_offset_x") cfg::target_offset_x = std::stoi(valueStr);
 else if (key == "target_offset_y") cfg::target_offset_y = std::stoi(valueStr);
 else if (key == "apply_delta_fov") cfg::apply_delta_fov = std::stoi(valueStr);
 else if (key == "apply_delta_smooth") cfg::apply_delta_smooth = std::stof(valueStr);
 else if (key == "speed") cfg::speed = std::stof(valueStr);
 else if (key == "sleep") cfg::sleep = std::stoi(valueStr);

 else if (key == "apply_deltaassist_ativo") cfg::apply_deltaassist_ativo = (valueStr == "1");
 else if (key == "assist_apply_deltakey") cfg::assist_apply_deltakey = std::stoi(valueStr);
 else if (key == "assist_target_offset_x") cfg::assist_target_offset_x = std::stoi(valueStr);
 else if (key == "assist_target_offset_y") cfg::assist_target_offset_y = std::stoi(valueStr);
 else if (key == "apply_deltaassist_fov") cfg::apply_deltaassist_fov = std::stoi(valueStr);
 else if (key == "apply_deltaassist_smooth") cfg::apply_deltaassist_smooth = std::stof(valueStr);
 else if (key == "assist_speed") cfg::assist_speed = std::stof(valueStr);
 else if (key == "assist_sleep") cfg::assist_sleep = std::stoi(valueStr);

 else if (key == "nonmode_a_ativo") cfg::nonmode_a_ativo = (valueStr == "1");
 else if (key == "nonmode_a_key") cfg::nonmode_a_key = std::stoi(valueStr);
 else if (key == "nonmode_a_fov") cfg::nonmode_a_fov = std::stoi(valueStr);
 else if (key == "nonmode_a_delay_between_shots") cfg::nonmode_a_delay_between_shots = std::stoi(valueStr);
 else if (key == "nonmode_a_distance") cfg::nonmode_a_distance = std::stof(valueStr);

 else if (key == "mode_a_ativo") cfg::mode_a_ativo = (valueStr == "1");
 else if (key == "mode_a_key") cfg::mode_a_key = std::stoi(valueStr);
 else if (key == "mode_a_target_offset_x") cfg::mode_a_target_offset_x = std::stoi(valueStr);
 else if (key == "mode_a_target_offset_y") cfg::mode_a_target_offset_y = std::stoi(valueStr);
 else if (key == "mode_a_fov") cfg::mode_a_fov = std::stoi(valueStr);
 else if (key == "mode_a_delay_between_shots") cfg::mode_a_delay_between_shots = std::stoi(valueStr);
 else if (key == "distance") cfg::distance = std::stof(valueStr);
 else if (key == "mode_a_head_targeting") cfg::mode_a_head_targeting = (valueStr == "1");
 else if (key == "mode_a_cooldown_ms") cfg::mode_a_cooldown_ms = std::stoi(valueStr);

 else if (key == "trigger_action_ativo") cfg::trigger_action_ativo = (valueStr == "1");
 else if (key == "trigger_action_key") cfg::trigger_action_key = std::stoi(valueStr);
 else if (key == "trigger_action_delay") cfg::trigger_action_delay = std::stoi(valueStr);
 else if (key == "trigger_action_fovX") cfg::trigger_action_fovX = std::stoi(valueStr);
 else if (key == "trigger_action_fovY") cfg::trigger_action_fovY = std::stoi(valueStr);
 else if (key == "useIstrigFilter") cfg::useIstrigFilter = (valueStr == "1");

 else if (key == "color_mode") cfg::color_mode = std::stoi(valueStr);
 else if (key == "ngrok") cfg::ngrok = std::stoi(valueStr);
 else if (key == "use_gpu_processing") cfg::use_gpu_processing = (valueStr == "1");

 else if (key == "dead_body_filter") cfg::dead_body_filter = (valueStr == "1");
 else if (key == "dead_body_threshold") cfg::dead_body_threshold = std::stoi(valueStr);
 else if (key == "min_cluster_size") cfg::min_cluster_size = std::stoi(valueStr);

 else if (key == "trigger_polygon_check") cfg::trigger_polygon_check = (valueStr == "1");
 else if (key == "apply_delta_dist_smoothing") cfg::apply_delta_dist_smoothing = (valueStr == "1");
 else if (key == "apply_delta_near_dist") cfg::apply_delta_near_dist = std::stoi(valueStr);
 else if (key == "apply_delta_mid_dist") cfg::apply_delta_mid_dist = std::stoi(valueStr);
 else if (key == "apply_delta_near_mult") cfg::apply_delta_near_mult = std::stof(valueStr);
 else if (key == "apply_delta_mid_mult") cfg::apply_delta_mid_mult = std::stof(valueStr);

 else if (key == "head_anchor_proportional") cfg::head_anchor_proportional = (valueStr == "1");
 else if (key == "head_anchor_band_rows") cfg::head_anchor_band_rows = std::stoi(valueStr);
 else if (key == "head_anchor_gap_tolerance") cfg::head_anchor_gap_tolerance = std::stoi(valueStr);
 else if (key == "head_anchor_close_pct") cfg::head_anchor_close_pct = std::stoi(valueStr);
 else if (key == "head_anchor_mid_pct") cfg::head_anchor_mid_pct = std::stoi(valueStr);
 else if (key == "head_anchor_close_min_h") cfg::head_anchor_close_min_h = std::stoi(valueStr);
 else if (key == "head_anchor_mid_min_h") cfg::head_anchor_mid_min_h = std::stoi(valueStr);

 else if (key.find("menorRGB") == 0 && key.length() == 9) {
 int index = key[8] - '0';
 if (index >= 0 && index < 3) cfg::menorRGB[index] = std::stoi(valueStr);
 }
 else if (key.find("maiorRGB") == 0 && key.length() == 9) {
 int index = key[8] - '0';
 if (index >= 0 && index < 3) cfg::maiorRGB[index] = std::stoi(valueStr);
 }
 else if (key.find("menorHSV") == 0 && key.length() == 9) {
 int index = key[8] - '0';
 if (index >= 0 && index < 3) cfg::menorHSV[index] = std::stoi(valueStr);
 }
 else if (key.find("maiorHSV") == 0 && key.length() == 9) {
 int index = key[8] - '0';
 if (index >= 0 && index < 3) cfg::maiorHSV[index] = std::stoi(valueStr);
 }
 }

 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 return true;
 }
 catch (const std::exception&) {
 return false;
 }
}

bool ConfigManager::loadIPAndPort(std::string& ip, int& port) {
 try {
 std::ifstream inFile(xorstr_("data"), std::ios::binary);
 if (!inFile.is_open()) return false;

 std::stringstream buffer;
 buffer << inFile.rdbuf();
 inFile.close();

 std::string decryptedData = encryptDecrypt(buffer.str());
 std::istringstream iss(decryptedData);
 std::string line;

 if (std::getline(iss, line)) {
 ip = line;
 if (std::getline(iss, line)) {
 try {
 port = std::stoi(line);
 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 return true;
 }
 catch (...) {
 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 return false;
 }
 }
 }
 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 return false;
 }
 catch (...) { return false; }
}

std::string ConfigManager::getLicenseFromConfig() {
 try {
 std::ifstream inFile(xorstr_("data"), std::ios::binary);
 if (!inFile.is_open()) return "";

 std::stringstream buffer;
 buffer << inFile.rdbuf();
 inFile.close();

 std::string decryptedData = encryptDecrypt(buffer.str());
 std::istringstream iss(decryptedData);
 std::string line;

 std::getline(iss, line); // IP
 std::getline(iss, line); // Port

 while (std::getline(iss, line)) {
 if (line.find(xorstr_("LICENSE_HWID=")) == 0) {
 std::string result = line.substr(13);
 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 return result;
 }
 if (line == xorstr_("---CONFIG_START---")) break;
 }

 SecureZeroMemory(&decryptedData[0], decryptedData.size());
 return "";
 }
 catch (...) { return ""; }
}
