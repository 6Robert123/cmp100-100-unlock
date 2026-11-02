#!/usr/bin/env python3
"""Build IFR candidate images from a pinned CMP100 SPI dump.

Named options change exactly one operand word each, with hard guards on the
source SHA, the record address, and the expected operand values. Every change
is checked to stay inside the declared word and the resulting IFR byte sum is
reported so the integrity hypothesis stays visible.

Usage:
  patch_ifr_records.py SRC OUT [--width 8|16] [--cya clear]

Records on these images:
  0x088084 LnkCap        operands (0xfffffc0f, 0x00000010)
  0x08841c PRIV_MISC_1   operands (0x3fff9fff, 0xc0006000)
"""
import hashlib
import struct
import sys

SRC_SHA = "8ae75ee4d1fc28e7c5e42982d16f726f40017b731ac046474e5207e82bb406ba"
SIZE = 524288

REC = {
    "0x088084": dict(off=0xAC, mask=0xFFFFFC0F, data=0x00000010),
    "0x08841C": dict(off=0xC0, mask=0x3FFF9FFF, data=0xC0006000),
    "0x08C040": dict(off=0x1EC, mask=0x07FFFFFF, data=0x08000000),
}
WIDTH = {8: 0x80, 16: 0x100}
# P100 alignment for the speed path: P100 writes nothing here, its live values
# are PRIV_MISC_1 0x00b40500 and LINK_CONFIG_0 0x80009000.
GEN2 = {"0x08841C": 0x00000000, "0x08C040": 0x80000000}


def byte_sum(d, end):
    return sum(d[:end]) % 256


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    src, out = args[0], args[1]
    width = None
    cya_clear = False
    gen2 = False
    i = 2
    while i < len(args):
        if args[i] == "--width":
            width = int(args[i + 1]); i += 2
        elif args[i] == "--cya":
            assert args[i + 1] == "clear"
            cya_clear = True; i += 2
        elif args[i] == "--gen2":
            gen2 = True; i += 1
        else:
            print("unknown option %s" % args[i], file=sys.stderr); return 2
    if width is None and not cya_clear and not gen2:
        print("nothing to do", file=sys.stderr)
        return 2

    orig = open(src, "rb").read()
    if len(orig) != SIZE:
        print("bad size", file=sys.stderr); return 1
    sha = hashlib.sha256(orig).hexdigest()
    if sha != SRC_SHA:
        print("source sha %s != pinned" % sha, file=sys.stderr); return 1
    end = struct.unpack_from("<I", orig, 8)[0] & 0xFFFFFF
    img = bytearray(orig)
    changes = []

    def patch(addr_hex, new_data):
        rec = REC[addr_hex]
        header = struct.unpack_from("<I", img, rec["off"])[0]
        if (header & 0xFFFFFC) != int(addr_hex, 16):
            raise SystemExit("record at %#x is %#x" % (rec["off"], header & 0xFFFFFC))
        mask, data = struct.unpack_from("<II", img, rec["off"] + 4)
        if (mask, data) != (rec["mask"], rec["data"]):
            raise SystemExit("unexpected operands at %s: %#x %#x" % (addr_hex, mask, data))
        struct.pack_into("<I", img, rec["off"] + 8, new_data)
        changes.append((addr_hex, rec["off"] + 8, data, new_data))

    if width is not None:
        if width not in WIDTH:
            print("width must be 8 or 16", file=sys.stderr); return 2
        patch("0x088084", WIDTH[width])
    if cya_clear:
        patch("0x08841C", 0x00006000)
    if gen2:
        for addr, val in GEN2.items():
            patch(addr, val)

    movers = [i for i in range(SIZE) if orig[i] != img[i]]
    allowed = set()
    for _, off, _, _ in changes:
        allowed |= {off, off + 1, off + 2, off + 3}
    if not set(movers) <= allowed:
        print("bytes changed outside declared words: %s" % movers, file=sys.stderr)
        return 1

    open(out, "wb").write(img)
    print("source_sha=%s" % sha)
    print("candidate_sha=%s" % hashlib.sha256(bytes(img)).hexdigest())
    for addr, off, old, new in changes:
        print("patch %s at %#x: %#010x -> %#010x" % (addr, off, old, new))
    print("changed_offsets=%s" % movers)
    print("ifr_byte_sum_mod256 %d -> %d  %s"
          % (byte_sum(orig, end), byte_sum(img, end),
             "OK" if byte_sum(img, end) == 0 else "BREAKS THE ZERO-SUM HYPOTHESIS"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
