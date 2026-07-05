#pragma once

#include "license.h"

namespace license_dialog {

// Shows a modal Win32 dialog prompting for a license key.
// On Activate: writes the entered key into outBuf (NUL-terminated) and
// returns true. On Cancel/close: returns false.
// outBuf must hold at least kLicenseLen + 1 bytes.
bool prompt(char outBuf[license::kLicenseLen + 1]);

} // namespace license_dialog
