"""
rotate_secrets.py  -  generate fresh values for every rotatable secret in
the coloruino codebase and print copy-pastable blocks + the exact files
and lines to edit.

Usage:
    python rotate_secrets.py

Run from anywhere.  Does not touch any source file; prints to stdout so
you can review before pasting.  Run multiple times until you like the
random output, then copy the blocks into the listed files.
"""

import secrets
import string
import sys


def hex_str(nbytes: int) -> str:
    """32 hex chars for 16 bytes, 24 for 12, etc.  Lower case."""
    return secrets.token_hex(nbytes)


def safe_username(length: int = 10) -> str:
    first = secrets.choice(string.ascii_lowercase)
    rest_alphabet = string.ascii_lowercase + string.digits
    return first + "".join(secrets.choice(rest_alphabet) for _ in range(length - 1))


def safe_password(length: int = 20) -> str:
    # Avoid characters that break HTTP Basic auth header parsing or URL encoding.
    alphabet = string.ascii_letters + string.digits + "!@#$%^&*"
    return "".join(secrets.choice(alphabet) for _ in range(length))


def safe_hashkey(length: int = 18) -> str:
    # ASCII alphanumeric only  -  used as XOR key bytes, no special chars.
    alphabet = string.ascii_letters + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(length))


def c_byte_array(b: bytes, indent: str = "    ", per_row: int = 8) -> str:
    rows = []
    for i in range(0, len(b), per_row):
        chunk = b[i:i + per_row]
        line = ", ".join(f"0x{x:02X}" for x in chunk)
        if i + per_row < len(b):
            line += ","
        rows.append(indent + line)
    return "{\n" + "\n".join(rows) + "\n}"


def hex_to_c_bytes(hex_str: str, indent: str = "    ", per_row: int = 8) -> str:
    b = bytes.fromhex(hex_str)
    return c_byte_array(b, indent=indent, per_row=per_row)


SEP = "=" * 78
DIV = "-" * 78


def main() -> int:
    license_key       = hex_str(16)            # 32 lowercase hex chars
    webui_user        = safe_username(10)
    webui_pass        = safe_password(20)
    config_xor_key    = hex_str(12)            # 24 hex chars; used as XOR cipher key + HWID salt
    license_hash_key  = safe_hashkey(18)
    proto_key_bytes   = secrets.token_bytes(16)

    print(SEP)
    print(" Coloruino  -  rotated secrets (run again for different output)")
    print(SEP)
    print()
    print(f" License key (32 lowercase hex):  {license_key}")
    print(f" WebUI username:                  {webui_user}")
    print(f" WebUI password:                  {webui_pass}")
    print(f" Config XOR key (24 hex / 12 B):  {config_xor_key}")
    print(f" License hash key (18 ASCII):     {license_hash_key}")
    print(f" Protocol XOR key (16 raw bytes): {proto_key_bytes.hex()}")
    print()

    # ────────────────────────────────────────────────────────────────────────
    print(SEP)
    print(" 1. LICENSE KEY")
    print(SEP)
    print(f' Replace the old 32-hex literal with:  "{license_key}"')
    print()
    print(" Files (find: ct_fnv1a(\"<old 32 hex>\", 32)):")
    print("   • coloruino-app/coloruino5500/src/security/LicenseManager.cpp")
    print("        -  around line 35 (constant: VALID_KEY_HASH)")
    print("   • coloruino-loader/Process Hollowing/license.cpp")
    print("        -  around line 28 (function: ct_fnv1a)")
    print("   • coloruino-config-generator/config_generator.cpp")
    print(f"        -  around line 29 (VALID_LICENSE = \"{license_key}\")")
    print()

    # ────────────────────────────────────────────────────────────────────────
    print(SEP)
    print(" 2. WEBUI BASIC AUTH")
    print(SEP)
    print(f' Replace:  xorstr_("<old user>")  ->  xorstr_("{webui_user}")')
    print(f' Replace:  xorstr_("<old pass>")  ->  xorstr_("{webui_pass}")')
    print()
    print(" File:  coloruino-app/coloruino5500/src/security/Auth.cpp")
    print("        -  lines 62-63 (inside ComputeAuthHash)")
    print()

    # ────────────────────────────────────────────────────────────────────────
    print(SEP)
    print(" 3. CONFIG XOR KEY + HWID SALT (same value, FOUR places  -  keep in sync)")
    print(SEP)
    print(f' Replace the old 24-hex literal with:  "{config_xor_key}"')
    print()
    print(" Files (find: \"<your 24-hex XOR key>\" or current 24-hex value):")
    print("   • coloruino-app/coloruino5500/src/core/ConfigManager.cpp")
    print("        -  around line 14 (key in encryptDecrypt)")
    print("   • coloruino-config-generator/config_generator.cpp")
    print("        -  around lines 26-27 (ENCRYPT_KEY + HWID_SALT  -  set BOTH")
    print("         to the same new value)")
    print("   • coloruino-app/coloruino5500/src/security/LicenseManager.cpp")
    print("        -  around line 83 (salt inside generateHWID)")
    print("   • coloruino-loader/Process Hollowing/data_writer.cpp")
    print("        -  TWO occurrences:")
    print("         * salt literal inside generate_app_hwid()")
    print("         * key literal inside xor_encrypt()")
    print("         BOTH must match the app side or `data` written by the")
    print("         loader won't decrypt for the app  -  silent exit.")
    print()
    print(" Loader-side equivalent  -  replace the kHwidSalt byte array with:")
    print()
    print(" coloruino-loader/Process Hollowing/hwid.cpp  (around line 53)")
    print(" -----")
    print(" constexpr uint8_t kHwidSalt[] = " + hex_to_c_bytes(config_xor_key) + ";")
    print(" -----")
    print()

    # ────────────────────────────────────────────────────────────────────────
    print(SEP)
    print(" 4. LICENSE HASH KEY")
    print(SEP)
    print(f' Replace the old ASCII literal with:  "{license_hash_key}"')
    print()
    print(" Files (find: \"<your ASCII hash key>\" or current value):")
    print("   • coloruino-app/coloruino5500/src/security/LicenseManager.cpp")
    print("        -  around line 95 (key inside hashHWID)")
    print("   • coloruino-config-generator/config_generator.cpp")
    print("        -  around line 28 (HASH_KEY)")
    print("   • coloruino-loader/Process Hollowing/data_writer.cpp")
    print("        -  inside hash_app_hwid() (must match app's hashHWID byte-for-byte)")
    print()

    # ────────────────────────────────────────────────────────────────────────
    print(SEP)
    print(" 5. PROTOCOL XOR KEY (firmware + PC must match byte-for-byte)")
    print(SEP)
    print(" Replace the kProtoKey[16] = { ... } array in BOTH files with:")
    print()
    print(" -----")
    print(" static const uint8_t kProtoKey[16] = " + c_byte_array(proto_key_bytes) + ";")
    print(" -----")
    print()
    print(" Files (find: kProtoKey[16] = {):")
    print("   • coloruino-fw/coloruino-fw.ino")
    print("        -  around line 200 (top of the network protocol block)")
    print("   • coloruino-app/coloruino5500/src/network/UDPClient.cpp")
    print("        -  around line 35 (anonymous namespace)")
    print()
    print(" After editing both: rebuild coloruino-app AND re-flash coloruino-fw.")
    print(" Mismatch = Arduino silently drops every packet (no movement, no fire).")
    print()

    # ────────────────────────────────────────────────────────────────────────
    print(SEP)
    print(" Build-time per-release secrets (no manual rotation needed)")
    print(SEP)
    print()
    print("   build_secrets.h   in coloruino-loader/Process Hollowing/")
    print("     → regenerated automatically by gen_build_secrets.py (the")
    print("       loader vcxproj has a PreBuildEvent that runs it on every")
    print("       build).  To force fresh values: delete build_secrets.h.")
    print()
    print("   auth.dat   written by the loader next to AMDRSHelper.exe on")
    print("              first successful license entry; persists thereafter.")
    print("   data       written by the loader (data_writer.cpp) next to")
    print("              AMDRSHelper.exe on first run; rewritten by the app")
    print("              when the user changes WebUI settings.")
    print("     → both are per-machine, HWID-bound, never committed.")
    print("     → config_generator.exe is NOT part of client deployment any more;")
    print("       it remains in the repo for supplier-side debug (force-rebuild")
    print("       `data` without going through the loader's license prompt).")
    print()
    print(SEP)
    print(" Post-rotation checklist (dev PC)")
    print(SEP)
    print()
    print("   [ ] Edited every file listed above (cross-check section 3  -  FOUR files share the salt).")
    print("   [ ] Rebuilt coloruino-app (pipanel.exe).")
    print("   [ ] VMProtect-packed coloruino-app.")
    print("   [ ] HxD dump → TabTip32_exe_bytes.h (with #include \"VMProtectSDK.h\").")
    print("   [ ] python gen_build_secrets.py  (or delete build_secrets.h and let PreBuildEvent regen)")
    print(f"   [ ] python encrypt_payload_aes.py {license_key}")
    print("   [ ] Rebuilt coloruino-loader (AMDRSHelper.exe).")
    print("   [ ] sanitize_pe.py ran via PostBuildEvent (check build log).")
    print("   [ ] tools/signing/02_sign_binary.ps1 against AMDRSHelper.exe.")
    print("   [ ] Re-flashed Arduino firmware (only if PROTOCOL XOR KEY changed).")
    print()
    print(SEP)
    print(" Per-client checklist (AnyDesk into each client)")
    print(SEP)
    print()
    print("   [ ] Delete old AMDRSHelper.exe + auth.dat + data on the client.")
    print("   [ ] Copy the freshly built/signed AMDRSHelper.exe over.")
    print("   [ ] (If signing identity also rotated) run 03_install_root_cert.ps1 admin.")
    print("   [ ] Launch AMDRSHelper.exe.")
    print(f"   [ ] Paste {license_key} into the dialog YOU SEE, click OK.")
    print("   [ ] Confirm cursor twitches once + auth.dat + data appear.")
    print("   [ ] Open http://localhost:13548/ → Test → cursor twitches → green.")
    print("   [ ] Disconnect AnyDesk.")
    print()
    print(" Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
