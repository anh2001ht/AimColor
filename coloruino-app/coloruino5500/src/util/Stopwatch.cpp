#include "Stopwatch.h"

stopwatch::stopwatch() {
 update();
}

void stopwatch::update() {
 QueryPerformanceCounter(&start_time);
}

double stopwatch::get_elapsed() {
 static const LARGE_INTEGER frequency = [] {
 LARGE_INTEGER f;
 QueryPerformanceFrequency(&f);
 return f;
 }();
 LARGE_INTEGER current_time;
 QueryPerformanceCounter(&current_time);
 LONGLONG elapsed = current_time.QuadPart - start_time.QuadPart;
 return (static_cast<double>(elapsed) * 1000.0) / static_cast<double>(frequency.QuadPart);
}
