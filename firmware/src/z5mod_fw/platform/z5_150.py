"""Nikon Z 5, firmware C 1.50.

Every number here was confirmed against the official package; nothing is guessed.
A different firmware version needs its own module, not an edit to this one.
"""
from pathlib import Path

CAMERA = 'Nikon Z 5'
FIRMWARE = 'C 1.50'

# --- the stock package -----------------------------------------------------
PACKAGE_SIZE = 36011790
PACKAGE_SHA256 = 'efc0ce70dcafdd96c64e95d8d50f6b7f0509fc0c2840b28d12b6f9e4877ee776'

HDR = 0x20                  # plaintext header before the body
SPACE_LABEL_LEN = 0x20      # body[0:0x20] is 32 spaces
TRAILER = 16                # BE16 package CRC followed by 14 zero bytes

# --- body layout -----------------------------------------------------------
TABLE = 0x20                # module directory
ENTRY = 32                  # 16-byte name, BE32 offset, BE32 length, 8 zero
DIR_HEADER = 16             # BE32 count, BE32 directory size, 8 zero

# --- the module we patch ---------------------------------------------------
MODULE_MAIN = 'eg1650_015001.bi'
MODULE_MAIN_SIZE = 34603578
MODULE_MAIN_SHA256 = '1d9742a951ea8a8ebe695fa9bb1f29a3be73e16dfe4a8372d997e903bbc58e98'

# --- the XOR mask ----------------------------------------------------------
# plain[HDR + i] = cipher[HDR + i] ^ T1[i & 0xFF] ^ T2[(i >> 8) & 0xFF]
#                                         ^ T3[(i >> 16) & 0xFF]
MASK_FILE = Path(__file__).with_name('z5_150_mask.hex')
MASK_LEN = 768              # three 256-byte tables


def mask_bytes() -> bytes:
    """Read the mask table; the file is hex text so it stays reviewable."""
    lines = [line.strip() for line in MASK_FILE.read_text().splitlines()
             if line.strip() and not line.startswith("#") and not line.endswith(":")]
    return bytes.fromhex("".join(lines))
