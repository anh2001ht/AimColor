"""
encrypt_payload_aes.py  -  AES-256-CBC encrypt TabTip32_exe_bytes.h in place
using a key derived from the license key + per-build salt.

Pipeline:
    1. Run gen_build_secrets.py once per release build.
    2. Replace TabTip32_exe_bytes.h with the HxD export of the packed
       coloruino-app payload (plain MZ/PE).
    3. Run this script with the license key as the single argument.
    4. Build the loader.

The loader's runtime decrypt uses the same key derivation
    payload_key = SHA256(license_bytes || kBuildSalt)
and the same kPayloadIV from build_secrets.h.

Usage:
    python encrypt_payload_aes.py <license_key_32_lowercase_hex>
"""

import hashlib
import os
import re
import sys

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad
except ImportError:
    print("ERROR: requires pycryptodome.  Run: pip install pycryptodome")
    sys.exit(1)

HERE = os.path.dirname(os.path.abspath(__file__))
SECRETS_PATH = os.path.join(HERE, "build_secrets.h")
BYTES_PATH   = os.path.join(HERE, "TabTip32_exe_bytes.h")


def parse_byte_array(text, name):
    m = re.search(rf"{name}\s*\[\s*\]\s*=\s*\{{([^}}]+)\}}", text)
    if not m:
        raise RuntimeError(f"{name} not found")
    return bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1

    license_key = sys.argv[1].strip()
    if len(license_key) != 32 or not all(c in "0123456789abcdef" for c in license_key):
        print("ERROR: license must be 32 lowercase hex characters")
        return 1

    if not os.path.isfile(SECRETS_PATH):
        print(f"ERROR: {SECRETS_PATH} missing.  Run gen_build_secrets.py first.")
        return 1
    if not os.path.isfile(BYTES_PATH):
        print(f"ERROR: {BYTES_PATH} missing.")
        return 1

    with open(SECRETS_PATH, "r") as f:
        secrets_text = f.read()
    salt = parse_byte_array(secrets_text, "kBuildSalt")
    iv   = parse_byte_array(secrets_text, "kPayloadIV")
    if len(salt) != 32 or len(iv) != 16:
        print("ERROR: build_secrets.h has wrong sizes  -  re-run gen_build_secrets.py")
        return 1

    payload_key = hashlib.sha256(license_key.encode("ascii") + salt).digest()

    with open(BYTES_PATH, "r") as f:
        text = f.read()

    array_m = re.search(
        r"unsigned char TabTip32_exe\[(\d+)\]\s*=\s*\{([\s\S]*?)\};", text)
    if not array_m:
        print("ERROR: TabTip32_exe array not found")
        return 1
    decl_size = int(array_m.group(1))
    body = array_m.group(2)

    raw = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    if len(raw) != decl_size:
        print(f"ERROR: array size mismatch: decl={decl_size}, actual={len(raw)}")
        return 1
    if raw[:2] != b"\x4D\x5A":
        print("ERROR: payload first bytes are not MZ  -  bytes file may already be encrypted")
        return 1

    cipher     = AES.new(payload_key, AES.MODE_CBC, iv)
    ciphertext = cipher.encrypt(pad(raw, AES.block_size))

    new_lines = []
    for i in range(0, len(ciphertext), 12):
        chunk = ciphertext[i:i + 12]
        line  = "\t" + ", ".join(f"0x{x:02X}" for x in chunk)
        if i + 12 < len(ciphertext):
            line += ","
        new_lines.append(line)
    new_body = "\n".join(new_lines)
    new_decl = f"unsigned char TabTip32_exe[{len(ciphertext)}] = {{\n{new_body}\n}};"

    new_text = text[:array_m.start()] + new_decl + text[array_m.end():]
    with open(BYTES_PATH, "w") as f:
        f.write(new_text)

    print(f"Encrypted: {len(raw)} → {len(ciphertext)} bytes (added {len(ciphertext) - len(raw)} bytes PKCS#7 padding)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
