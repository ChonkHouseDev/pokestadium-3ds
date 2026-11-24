#!/usr/bin/env python3
"""Checks that a BPS patch carries no data from the ROM.

Usage: python check_patch.py <patch.bps> <baserom.z64>

A BPS patch either copies bytes from the user's ROM (SourceRead/SourceCopy) or
writes new bytes stored inside the patch (TargetRead). This script reads every
TargetRead block and fails if any run of MIN_RUN bytes or more also appears in
the ROM, which would mean ROM data is being distributed inside the patch.
"""
import sys, zlib

MIN_RUN = 24   # any ROM match this long or longer fails the check
WIN = 16       # window indexed from the ROM (every 8 bytes)
STEP = 8       # a match of MIN_RUN bytes always contains one indexed window


def varint(buf, p):
    data, shift = 0, 1
    while True:
        x = buf[p]; p += 1
        data += (x & 0x7F) * shift
        if x & 0x80:
            return data, p
        shift <<= 7
        data += shift


def literals(patch):
    if patch[:4] != b"BPS1":
        sys.exit("Not a BPS patch.")
    p = 4
    src_size, p = varint(patch, p)
    dst_size, p = varint(patch, p)
    meta, p = varint(patch, p)
    p += meta
    out, end, outpos = [], len(patch) - 12, 0
    while p < end:
        data, p = varint(patch, p)
        cmd, length = data & 3, (data >> 2) + 1
        if cmd == 1:                       # TargetRead: new bytes stored in the patch
            out.append((outpos, patch[p:p + length])); p += length
        elif cmd in (2, 3):                # SourceCopy / TargetCopy: relative offset
            _, p = varint(patch, p)
        outpos += length
    src_crc, dst_crc = int.from_bytes(patch[end:end + 4], "little"), int.from_bytes(patch[end + 4:end + 8], "little")
    return src_size, dst_size, src_crc, dst_crc, out


def boring(w):
    return w.count(w[0]) == len(w)         # all the same byte (padding, zeros)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    patch = open(sys.argv[1], "rb").read()
    rom = open(sys.argv[2], "rb").read()
    src_size, dst_size, src_crc, dst_crc, lits = literals(patch)
    if src_size != len(rom) or zlib.crc32(rom) != src_crc:
        sys.exit("This patch was not made from this ROM (size or CRC32 differ).")
    index = set()
    for i in range(0, len(rom) - WIN + 1, STEP):
        w = rom[i:i + WIN]
        if not boring(w):
            index.add(hash(w))
    total = sum(len(b) for _, b in lits)
    bad = []
    for pos, block in lits:
        if len(block) < MIN_RUN:
            continue
        for j in range(len(block) - WIN + 1):
            w = block[j:j + WIN]
            if not boring(w) and hash(w) in index and w in rom:
                bad.append(pos + j)
                break
    print(f"Patch: {len(patch)} bytes | output: {dst_size} bytes | new bytes in patch: {total}")
    if bad:
        print(f"FAIL: {len(bad)} block(s) of the patch contain ROM data (first at output offset 0x{bad[0]:X}).")
        print("Check that the .cia/.3dsx is not compressed and that the patch was made in delta mode.")
        sys.exit(1)
    print("OK: no ROM data found inside the patch.")


if __name__ == "__main__":
    main()
