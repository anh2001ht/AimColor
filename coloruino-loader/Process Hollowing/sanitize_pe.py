"""
sanitize_pe.py  -  strip leaky metadata from a built PE binary.

Removed:
    * Rich header (between DOS stub and PE signature)  -  leaks the
      MSVC linker/compiler version mix used in the build.
    * TimeDateStamp  -  leaks the exact build wall-clock time.
    * Debug data directory pointer  -  even when GenerateDebugInformation
      is off there can be lingering codeview entries; zero the directory
      entry so tools think there's no debug info.
    * Section names randomized  -  default ".text" / ".rdata" / ".data" /
      ".pdata" / ".xdata" are predictable fingerprints; replaced with
      random 8-char alphanumeric strings per build.  Windows loader
      keys off data directories, not names, so this is safe.

Invoked as a vcxproj PostBuildEvent.

Usage:
    python sanitize_pe.py <path_to_exe>
"""

import os
import secrets
import string
import struct
import sys


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    if not os.path.isfile(path):
        print(f"ERROR: file not found: {path}")
        return 1

    with open(path, "rb") as f:
        data = bytearray(f.read())

    # DOS header sanity.
    if len(data) < 0x40 or data[0:2] != b"MZ":
        print("ERROR: not a PE (no MZ)")
        return 1

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew < 0x80 or e_lfanew + 24 > len(data):
        print(f"ERROR: implausible e_lfanew={e_lfanew}")
        return 1
    if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        print(f"ERROR: PE signature missing at e_lfanew={e_lfanew}")
        return 1

    # Rich header sits between the DOS stub (ends ~0x80) and the PE
    # signature.  Wipe the whole region.
    rich_start = 0x80
    rich_end = e_lfanew
    rich_len = 0
    if rich_end > rich_start:
        rich_len = rich_end - rich_start
        for i in range(rich_start, rich_end):
            data[i] = 0

    # TimeDateStamp lives in IMAGE_FILE_HEADER at +4 from the PE sig.
    struct.pack_into("<I", data, e_lfanew + 8, 0)

    # Debug data directory entry  -  IMAGE_OPTIONAL_HEADER.DataDirectory[6].
    # PE32+ has DataDirectory[0] at +112 from OptHdr start (= e_lfanew+24).
    # PE32  has DataDirectory[0] at  +96.
    magic = struct.unpack_from("<H", data, e_lfanew + 24)[0]
    if magic == 0x20B:        # PE32+
        debug_dir = e_lfanew + 24 + 112 + 6 * 8
    elif magic == 0x10B:      # PE32
        debug_dir = e_lfanew + 24 + 96 + 6 * 8
    else:
        debug_dir = None

    if debug_dir is not None and debug_dir + 8 <= len(data):
        struct.pack_into("<II", data, debug_dir, 0, 0)

    # Section name randomization.
    #
    # IMAGE_FILE_HEADER layout: SizeOfOptionalHeader at +16 (WORD),
    # NumberOfSections at +2 (WORD).  Section headers start right after
    # the optional header.
    num_sections = struct.unpack_from("<H", data, e_lfanew + 4 + 2)[0]
    size_of_opt  = struct.unpack_from("<H", data, e_lfanew + 4 + 16)[0]
    sect_start   = e_lfanew + 4 + 20 + size_of_opt

    alphabet = string.ascii_lowercase + string.digits
    renamed = 0
    for i in range(num_sections):
        off = sect_start + i * 40
        if off + 8 > len(data):
            break
        # 8-char lowercase + digit name with a leading "." for cosmetics.
        new_name = "." + "".join(secrets.choice(alphabet) for _ in range(7))
        data[off:off + 8] = new_name.encode("ascii").ljust(8, b"\x00")
        renamed += 1

    # Recalculate PE CheckSum  -  critical!  After modifying headers above
    # the linker-computed checksum is stale.  BCrypt/CNG APIs on modern
    # Windows (especially with VBS/HVCI) may silently fail when invoked
    # from a module whose PE checksum doesn't match, causing AES decrypt
    # to return an error and the payload to never launch.
    #
    # CheckSum lives at IMAGE_OPTIONAL_HEADER offset +64 for both PE32
    # and PE32+ (= e_lfanew + 4 + 20 + 64 = e_lfanew + 88).
    checksum_off = e_lfanew + 4 + 20 + 64
    # Zero the old checksum before computing the new one.
    struct.pack_into("<I", data, checksum_off, 0)

    checksum = 0
    # Standard PE checksum: sum all 16-bit words (skipping the checksum
    # field itself), fold carries, then add file length.
    for i in range(0, len(data) - (len(data) % 2), 2):
        if i == checksum_off or i == checksum_off + 2:
            continue
        val = struct.unpack_from("<H", data, i)[0]
        checksum += val
        checksum = (checksum & 0xFFFF) + (checksum >> 16)
    if len(data) % 2:
        checksum += data[-1]
        checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum += len(data)
    checksum &= 0xFFFFFFFF

    struct.pack_into("<I", data, checksum_off, checksum)

    with open(path, "wb") as f:
        f.write(data)

    print(f"sanitize_pe: rich={rich_len} bytes zeroed, "
          f"timestamp zeroed, "
          f"debug dir {'cleared' if debug_dir else 'n/a'}, "
          f"{renamed} sections renamed, "
          f"checksum=0x{checksum:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
