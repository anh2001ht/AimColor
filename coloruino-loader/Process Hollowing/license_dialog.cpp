#include "license_dialog.h"
#include "license_dialog_resource.h"

#include <Windows.h>

namespace license_dialog {

namespace {

struct DlgData {
 char* outBuf;
 size_t outBufCap;
};

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
 DlgData* d = reinterpret_cast<DlgData*>(GetWindowLongPtrA(hDlg, GWLP_USERDATA));
 switch (msg) {
 case WM_INITDIALOG:
 SetWindowLongPtrA(hDlg, GWLP_USERDATA, lParam);
 SendDlgItemMessageA(hDlg, IDC_EDIT_LICENSE, EM_SETLIMITTEXT,
 license::kLicenseLen, 0);
 SetFocus(GetDlgItem(hDlg, IDC_EDIT_LICENSE));
 return FALSE;
 case WM_COMMAND:
 switch (LOWORD(wParam)) {
 case IDOK:
 if (d && d->outBuf && d->outBufCap > 0) {
 GetDlgItemTextA(hDlg, IDC_EDIT_LICENSE,
 d->outBuf, static_cast<int>(d->outBufCap));
 }
 EndDialog(hDlg, IDOK);
 return TRUE;
 case IDCANCEL:
 EndDialog(hDlg, IDCANCEL);
 return TRUE;
 }
 break;
 case WM_CLOSE:
 EndDialog(hDlg, IDCANCEL);
 return TRUE;
 }
 return FALSE;
}

} // namespace

bool prompt(char outBuf[license::kLicenseLen + 1]) {
 if (!outBuf) return false;
 outBuf[0] = '\0';

 DlgData d = { outBuf, license::kLicenseLen + 1 };
 INT_PTR r = DialogBoxParamA(
 GetModuleHandleA(nullptr),
 MAKEINTRESOURCEA(IDD_LICENSE_DIALOG),
 nullptr, DlgProc,
 reinterpret_cast<LPARAM>(&d));
 return r == IDOK;
}

} // namespace license_dialog
