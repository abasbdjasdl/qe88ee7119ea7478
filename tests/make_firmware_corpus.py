"""Synthetic format seeds, never patches to proprietary firmware."""
import pathlib
import struct

root = pathlib.Path("build/firmware-corpus")
root.mkdir(parents=True, exist_ok=True)
for section_type in (1, 2, 9):
    for dynamic in (False, True):
        header = 64 if dynamic else 48
        blob = bytearray(32 + header + 64)
        blob[0:2] = bytes([255, 1])
        blob[16:19] = bytes([1, 5, 0])
        struct.pack_into("<II", blob, 20, 32, len(blob) - 32)
        struct.pack_into("<I", blob, 32, 0x88520102)
        blob[57] = 1
        struct.pack_into("<II", blob, 64, 0x18970000, section_type << 24 | 64)
        if dynamic:
            blob[46] = header
            struct.pack_into("<I", blob, 60, 1 << 16)
            struct.pack_into("<I", blob, 80, 16)
        (root / f"type-{section_type}-dynamic-{int(dynamic)}").write_bytes(blob)
