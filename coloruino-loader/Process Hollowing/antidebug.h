#pragma once

// Multi-threaded anti-debug for the loader. Adapted from umium
// (Apache 2.0, hotline1337) - slimmer subset of checks that fit our
// loader's lifecycle. Runs a 1-second polling thread plus a synchronous
// pre-license check + ntdll patch at install time.
//
// Usage: call antidebug::install() once at the very top of main().

namespace antidebug {

// Installs anti-debug:
// - synchronous early check; if any tripwire fires here, terminates
// the process before the license dialog or AES decrypt can run;
// - patches ntdll!DbgBreakPoint + ntdll!DbgUiRemoteBreakin so that
// debugger attach attempts redirect to ExitProcess;
// - spawns a detached background thread that re-runs the full check
// set every ~1 s for the lifetime of the loader process.
void install();

} // namespace antidebug
