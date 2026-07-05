#include "ColorDetector.h"
#include "core/Config.h"
#include "util/MathHelpers.h"

// ── MonitorInfo ──

int MonitorInfo::cachedRefreshRate = 60;
bool MonitorInfo::initialized = false;

int MonitorInfo::GetRefreshRate() {
 if (!initialized) {
 DEVMODE devMode = {};
 devMode.dmSize = sizeof(DEVMODE);
 cachedRefreshRate = EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode)
 ? devMode.dmDisplayFrequency : 60;
 initialized = true;
 }
 return cachedRefreshRate;
}

// ── FastColorDetector ──

std::array<bool, 256 * 256 * 256> FastColorDetector::lookupTable;
bool FastColorDetector::tableBuilt = false;
int FastColorDetector::lastColorMode = -1;
bool FastColorDetector::lastUseIstrigFilter = false;

void FastColorDetector::BuildTable() {
 for (int r = 0; r < 256; ++r) {
 for (int g = 0; g < 256; ++g) {
 for (int b = 0; b < 256; ++b) {
 int idx = (r << 16) | (g << 8) | b;
 lookupTable[idx] = IsTargetColorSlow(r, g, b);
 }
 }
 }
 tableBuilt = true;
 lastColorMode = cfg::color_mode;
 lastUseIstrigFilter = cfg::useIstrigFilter;
}

bool FastColorDetector::IsTargetColorSlow(int r, int g, int b) {
 int h, s, v;
 int maxVal = FastMax(FastMax(r, g), b);
 int minVal = FastMin(FastMin(r, g), b);
 int delta = maxVal - minVal;

 v = (maxVal * 100) / 255;
 s = (maxVal == 0) ? 0 : (delta * 100) / maxVal;

 if (delta == 0) {
 h = 0;
 }
 else if (maxVal == r) {
 h = ((g - b) * 60) / delta;
 if (h < 0) h += 360;
 }
 else if (maxVal == g) {
 h = ((b - r) * 60) / delta + 120;
 }
 else {
 h = ((r - g) * 60) / delta + 240;
 }

 if (cfg::useIstrigFilter) {
 switch (cfg::color_mode) {
 case 0:
 case 1:
 if (g >= 170) return false;
 if (g >= 120) {
 return FastAbs(r - b) <= 8
 && r - g >= 50 && b - g >= 50
 && r >= 105 && b >= 105;
 }
 return FastAbs(r - b) <= 13
 && r - g >= 60 && b - g >= 60
 && r >= 110 && b >= 100;
 case 2:
 return (r >= 168 && r <= 255 &&
 g >= 168 && g <= 255 &&
 b >= 0 && b <= 110 &&
 h >= 55 && h <= 65 &&
 s >= 5 && s <= 100 &&
 v >= 70 && v <= 100);
 case 3:
 return (r >= 225 && r <= 255 &&
 g >= 45 && g <= 136 &&
 b >= 45 && b <= 136 &&
 (h <= 30 || h >= 330) &&
 s >= 37 && s <= 80 &&
 v >= 88 && v <= 100);
 }
 }
 else {
 if (r < cfg::menorRGB[0] || r > cfg::maiorRGB[0]) return false;
 if (g < cfg::menorRGB[1] || g > cfg::maiorRGB[1]) return false;
 if (b < cfg::menorRGB[2] || b > cfg::maiorRGB[2]) return false;
 return h >= cfg::menorHSV[0] && h <= cfg::maiorHSV[0]
 && s >= cfg::menorHSV[1] && s <= cfg::maiorHSV[1]
 && v >= cfg::menorHSV[2] && v <= cfg::maiorHSV[2];
 }
 return false;
}

void FastColorDetector::EnsureTable() {
 if (!tableBuilt || lastColorMode != cfg::color_mode || lastUseIstrigFilter != cfg::useIstrigFilter) {
 BuildTable();
 }
}
