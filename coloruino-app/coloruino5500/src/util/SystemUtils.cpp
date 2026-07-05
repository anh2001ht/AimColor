#include "SystemUtils.h"

void set_process_priority(DWORD priority_class) {
 SetPriorityClass(::GetCurrentProcess(), priority_class);
}

void set_timer_resolution() {
 static NTSTATUS(NTAPI * nt_set_timer_resolution)
 (IN ULONG desired_resolution, IN BOOLEAN set_resolution, OUT PULONG current_resolution) =
 (NTSTATUS(NTAPI*)(ULONG, BOOLEAN, PULONG))
 ::GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution");

 if (!nt_set_timer_resolution) return;
 ULONG desired_resolution{ 5000UL }, current_resolution{};
 nt_set_timer_resolution(desired_resolution, TRUE, &current_resolution);
}

void set_thread_high_priority() {
 ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}
