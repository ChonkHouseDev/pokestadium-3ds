#!/usr/bin/env python3
"""verify_patch.py <patch.bps> <baserom.z64> [expected file]

Applies the patch to the ROM exactly as the patch page does, writes the result to build/verify/,
and, if an expected file is given (the .cia or .elf the patch was made from), checks they are identical."""
import os, sys, zlib


def varint(b, p):
    d, s = 0, 1
    while True:
        x = b[p]; p += 1
        d += (x & 0x7F) * s
        if x & 0x80:
            return d, p
        s <<= 7
        d += s


def apply(patch, src):
    if patch[:4] != b"BPS1":
        sys.exit("Not a BPS patch.")
    end = len(patch) - 12
    if zlib.crc32(patch[:end + 8]) != int.from_bytes(patch[end + 8:], "little"):
        sys.exit("The patch file is damaged (CRC32).")
    p = 4
    ss, p = varint(patch, p); ts, p = varint(patch, p); meta, p = varint(patch, p); p += meta
    if ss != len(src) or zlib.crc32(src) != int.from_bytes(patch[end:end + 4], "little"):
        sys.exit("This ROM is not the one the patch expects (size or CRC32 differ). Only Pokemon Stadium US 1.0 works.")
    out = bytearray(ts); o = sr = tr = 0
    while p < end:
        d, p = varint(patch, p); cmd, n = d & 3, (d >> 2) + 1
        if cmd == 0:
            out[o:o + n] = src[o:o + n]; o += n
        elif cmd == 1:
            out[o:o + n] = patch[p:p + n]; p += n; o += n
        else:
            v, p = varint(patch, p); v = (-1 if v & 1 else 1) * (v >> 1)
            if cmd == 2:
                sr += v; out[o:o + n] = src[sr:sr + n]; sr += n; o += n
            else:
                tr += v
                for _ in range(n):
                    out[o] = out[tr]; o += 1; tr += 1
    if zlib.crc32(out) != int.from_bytes(patch[end + 4:end + 8], "little"):
        sys.exit("The patched result is wrong (CRC32).")
    return bytes(out)


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    patch = open(sys.argv[1], "rb").read()
    out = apply(patch, open(sys.argv[2], "rb").read())
    name = os.path.basename(sys.argv[1])[:-4]
    name = name[:-4] + ".cia" if name.endswith("_cia") else name[:-4] + ".elf" if name.endswith("_elf") else name + ".bin"
    vdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "verify")
    os.makedirs(vdir, exist_ok=True)
    dst = os.path.join(vdir, name)
    open(dst, "wb").write(out)
    print(f"Patched OK: {os.path.normpath(dst)} ({len(out)} bytes)")
    if len(sys.argv) == 4:
        if open(sys.argv[3], "rb").read() == out:
            print("OK: identical to " + sys.argv[3])
        else:
            print("FAIL: different from " + sys.argv[3]); sys.exit(1)


if __name__ == "__main__":
    main()
