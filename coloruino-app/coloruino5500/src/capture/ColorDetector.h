#pragma once

#include <array>
#include <windows.h>

class MonitorInfo {
 static int cachedRefreshRate;
 static bool initialized;
public:
 static int GetRefreshRate();
};

class FastColorDetector {
private:
 static std::array<bool, 256 * 256 * 256> lookupTable;
 static bool tableBuilt;
 static int lastColorMode;
 static bool lastUseIstrigFilter;

 static void BuildTable();
 static bool IsTargetColorSlow(int r, int g, int b);

public:
 static void EnsureTable();
 static inline bool IsTargetColor(int r, int g, int b) {
 int idx = (r << 16) | (g << 8) | b;
 return lookupTable[idx];
 }
 static const std::array<bool, 256 * 256 * 256>& GetLookupTable() {
 return lookupTable;
 }
};
