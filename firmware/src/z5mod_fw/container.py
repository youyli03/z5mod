"""Decode and verify a stock package, then hand out its modules.

The package is an XOR-obfuscated body: a plaintext 32-byte header, then modules
listed in a directory, then a checksum trailer. Decoding is integrity-checked,
never authenticity-checked - the checks say the bytes are consistent, not that
Nikon built them.
"""
from __future__ import annotations

import binascii
import hashlib
import struct
from dataclasses import dataclass

from .platform import z5_150 as z5


class FormatError(ValueError):
    """The input is not the exact package this build supports."""


@dataclass(frozen=True)
class Module:
    name: str
    offset: int
    length: int
    data: bytes

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()


def _mask() -> bytes:
    data = z5.mask_bytes()
    if len(data) != z5.MASK_LEN:
        raise FormatError('mask table %s is %d bytes, expected %d'
                          % (z5.MASK_FILE.name, len(data), z5.MASK_LEN))
    return data


def _keystream(length: int, mask: bytes) -> bytes:
    """Build the mask stream for `length` body bytes.

    The stream repeats every 16 MiB, so one 64 KiB plane is built once and then
    re-keyed per 64 KiB block. Pure stdlib, but the heavy work stays in C-level
    big-integer operations instead of a per-byte Python loop.
    """
    t1, t2, t3 = mask[:256], mask[256:512], mask[512:]
    plane = b''.join(bytes(t1[x] ^ t2[j] for x in range(256)) for j in range(256))
    plane_int = int.from_bytes(plane, 'little')
    blocks = []
    for k in range((length + 0xFFFF) >> 16):
        fill = int.from_bytes(bytes([t3[k & 0xFF]]) * len(plane), 'little')
        blocks.append((plane_int ^ fill).to_bytes(len(plane), 'little'))
    return b''.join(blocks)[:length]


def decode(raw: bytes) -> bytes:
    """Return the plaintext body of the package."""
    if len(raw) != z5.PACKAGE_SIZE:
        raise FormatError('package is %d bytes, expected %d for %s %s'
                          % (len(raw), z5.PACKAGE_SIZE, z5.CAMERA, z5.FIRMWARE))
    digest = hashlib.sha256(raw).hexdigest()
    if digest != z5.PACKAGE_SHA256:
        raise FormatError('package sha256 is %s, expected %s for %s %s'
                          % (digest, z5.PACKAGE_SHA256, z5.CAMERA, z5.FIRMWARE))

    body = raw[z5.HDR:]
    # The mask is indexed by the offset inside the body. The space label that
    # opens the body is plaintext, so only the bytes after it are transformed.
    stream = _keystream(len(body), _mask())
    head, tail = body[:z5.SPACE_LABEL_LEN], body[z5.SPACE_LABEL_LEN:]
    tail_stream = stream[z5.SPACE_LABEL_LEN:]
    plain_tail = (int.from_bytes(tail, 'little') ^ int.from_bytes(tail_stream, 'little'))
    plain = head + plain_tail.to_bytes(len(tail), 'little')

    if plain[:z5.SPACE_LABEL_LEN] != b' ' * z5.SPACE_LABEL_LEN:
        raise FormatError('space label is not 32 spaces; the mask does not match')
    if plain[-z5.TRAILER + 2:] != bytes(z5.TRAILER - 2):
        raise FormatError('checksum trailer padding is not zero')
    stored = struct.unpack_from('>H', plain, len(plain) - z5.TRAILER)[0]
    actual = binascii.crc_hqx(plain[:-z5.TRAILER], 0)
    if stored != actual:
        raise FormatError('package CRC mismatch: stored 0x%04x, computed 0x%04x'
                          % (stored, actual))
    return plain


def modules(plain: bytes) -> list[Module]:
    """Parse and verify the module directory of a decoded body."""
    count, dirsize = struct.unpack_from('>II', plain, z5.TABLE)
    if not 0 < count <= 64:
        raise FormatError('invalid module count %d' % count)
    if dirsize != 48 + z5.ENTRY * count:
        raise FormatError('directory size %d does not match %d entries'
                          % (dirsize, count))
    if plain[z5.TABLE + 8:z5.TABLE + 16] != bytes(8):
        raise FormatError('directory reserved bytes are not zero')

    out, expected = [], dirsize
    for i in range(count):
        base = z5.TABLE + z5.DIR_HEADER + i * z5.ENTRY
        name = plain[base:base + 16].rstrip(b'\0').decode('ascii')
        start, length = struct.unpack_from('>II', plain, base + 16)
        if plain[base + 24:base + 32] != bytes(8):
            raise FormatError('module %s has nonzero reserved bytes' % name)
        if start != expected:
            raise FormatError('module %s starts at 0x%X, expected 0x%X'
                              % (name, start, expected))
        if start + length > len(plain) - z5.TRAILER:
            raise FormatError('module %s runs past the checksum trailer' % name)
        data = plain[start:start + length]
        stored = struct.unpack_from('>H', data, length - 2)[0]
        actual = binascii.crc_hqx(data[:-2], 0)
        if stored != actual:
            raise FormatError('module %s CRC mismatch: stored 0x%04x, computed 0x%04x'
                              % (name, stored, actual))
        out.append(Module(name, start, length, data))
        expected += length
    return out


def load(path) -> tuple[bytes, list[Module]]:
    """Decode a package from disk and return its body and modules."""
    raw = path.read_bytes()
    plain = decode(raw)
    return plain, modules(plain)


def main_module(mods: list[Module]) -> Module:
    """Return the main application module, verified against the known hash."""
    for mod in mods:
        if mod.name == z5.MODULE_MAIN:
            if mod.length != z5.MODULE_MAIN_SIZE or mod.sha256 != z5.MODULE_MAIN_SHA256:
                raise FormatError('%s is %d bytes with sha256 %s; expected %d / %s'
                                  % (mod.name, mod.length, mod.sha256,
                                     z5.MODULE_MAIN_SIZE, z5.MODULE_MAIN_SHA256))
            return mod
    raise FormatError('%s is not in this package' % z5.MODULE_MAIN)
