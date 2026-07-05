#pragma once

#include <Windows.h>

class stopwatch {
private:
 LARGE_INTEGER start_time;
public:
 stopwatch();
 void update();
 double get_elapsed();
};
