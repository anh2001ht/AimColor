#include "Globals.h"

std::mutex fovMutex;
int currentFOV = 0;
int Width = 0;
int Height = 0;
int oX = 0, oY = 0;

int apply_delta_x = 0, apply_delta_y = 0;
int mode_a_x = 0, mode_a_y = 0;
int nonmode_a_x = 0, nonmode_a_y = 0;

std::atomic<uint64_t> capture_seq{ 0 };
std::atomic<int> capture_fov_used{ 0 };
