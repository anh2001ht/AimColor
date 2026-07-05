// data_writer.h - emit the encrypted `data` config file that coloruino-app
// expects next to itself.
//
// In the supplier-via-AnyDesk deployment model, only AMDRSHelper.exe is
// ever permanent on the client. config_generator.exe used to be needed
// to produce `data`; this module folds that responsibility into the
// loader so the deployment is genuinely single-binary.
//
// The format mirrors coloruino-app/.../ConfigManager::saveConfig exactly:
// line 1: IP
// line 2: port
// line 3: LICENSE_HWID=<hex>
// line 4: ---CONFIG_START---
// XOR-encrypted with the config XOR key. No cfg::* values are written
// - the app's loadConfig falls back to compiled-in defaults for any
// key it doesn't find, which is exactly what we want for first-run.
//
// HWID computation here MUST match LicenseManager::generateHWID /
// LicenseManager::hashHWID in the app. If you rotate the HWID salt or
// the LicenseHashKey, update BOTH sides.

#pragma once

#include <string>

namespace data_writer {

// True if `data` already sits next to the running exe.
bool data_exists();

// Write `data` next to the running exe with IP/port defaults and an
// HWID hash computed for THIS machine. Returns false on any I/O or
// HWID-computation failure.
bool write_data_file(const std::string& ip = "192.168.1.216", int port = 5353);

// Convenience: write only if absent. Idempotent.
bool ensure_data_file();

} // namespace data_writer
