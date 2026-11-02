#!/usr/bin/env python3
"""Build a width-only IFR candidate from a pinned CMP100 SPI dump.

Reads the source image, changes exactly one 32-bit operand word in the
0x088084 record, recomputes nothing else, and asserts that only the expected
bytes moved. Does not talk to hardware and does not write into the archive.

Usage:
    python3 build_width_candidate.py <source.bin> <out.bin> [width]

width: 8 or 16 (default 16). Data word values: 0x80 -> x8, 0x100 -> x16.
Record layout on these images: record at 0xac, operand word at 0xb4.
"""
import hashlib
import os
import struct
import sys

RECORD_OFF = 0xAC
DATA_WORD_OFF = 0xB4
EXPECTED_SIZE = 524288
EXPECTED_ORIG = "8ae75ee4d1fc28e7c5e42982d16f726f40017b731ac046474e5207e82bb406ba"
DATA = {8: 0x80, 16: 0x100}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    if width not in DATA:
        print("width must be 8 or 16", file=sys.stderr)
        return 2

    orig = open(src, "rb").read()
    if len(orig) != EXPECTED_SIZE:
        print("unexpected image size %d" % len(orig), file=sys.stderr)
        return 1
    sha = hashlib.sha256(orig).hexdigest()
    if sha != EXPECTED_ORIG:
        print("source SHA is %s, expected pinned %s" % (sha, EXPECTED_ORIG), file=sys.stderr)
        return 1

    img = bytearray(orig)
    header = struct.unpack_from("<I", img, RECORD_OFF)[0]
    addr = header & 0xFFFFFC
    if addr != 0x088084:
        print("record at %#x is %#x, not LnkCap" % (RECORD_OFF, addr), file=sys.stderr)
        return 1
    mask, data = struct.unpack_from("<II", img, RECORD_OFF + 4)
    if mask != 0xFFFFFC0F or data != 0x10:
        print("unexpected operands %#x %#x, refusing" % (mask, data), file=sys.stderr)
        return 1

    struct.pack_into("<I", img, DATA_WORD_OFF, DATA[width])
    movers = [i for i in range(len(orig)) if orig[i] != img[i]]
    allowed = {DATA_WORD_OFF, DATA_WORD_OFF + 1, DATA_WORD_OFF + 2, DATA_WORD_OFF + 3}
    if not set(movers) <= allowed:
        print("changed bytes outside the data word: %s" % movers, file=sys.stderr)
        return 1

    open(dst, "wb").write(img)
    end = struct.unpack_from("<I", orig, 8)[0] & 0xFFFFFF
    print("source_sha=%s" % sha)
    print("candidate_sha=%s" % hashlib.sha256(bytes(img)).hexdigest())
    print("width=%d" % width)
    print("changed_byte_offsets=%s" % movers)
    print("ifr_byte_sum_mod256 %d -> %d"
          % (sum(orig[:end]) % 256, sum(img[:end]) % 256))
    print("note: no checksum field was recomputed; integrity rule is still open (A3)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
