#pragma once

inline int FastAbs(int x) { return (x ^ (x >> 31)) - (x >> 31); }
inline int FastMax(int a, int b) { return a > b ? a : b; }
inline int FastMin(int a, int b) { return a < b ? a : b; }
