#!/usr/bin/env python3
"""Offline structural regression checks for the four CMP100 NvBios images.

This never talks to hardware.  It verifies that the experimental variants only
contain the intended byte changes and that BIT M / M0203 remains intact.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from extract_gp100_cfg1_groups import bit_m, decode

FIRST_IMAGE_END = 0xF400
ROM_NAMES = {
    "stock": "cmp100_stock.rom",
    "nofbio": "cmp100_nofbio.rom",
    "s4": "cmp100_s4_4gb_16gb.rom",
    "cfg1_12gb": "cmp100_12gb_cfg1.rom",
}
EXPECTED_SHA256 = {
    "stock": "6628cadcf8c4b2b139f3592ebb965535fb7e4ed35bfc21e41022db6c450fd849",
    "nofbio": "3d39ab6489756f603496546d1b5762bf82d80b476ed12d2b7ba6495e80e195c5",
    "s4": "a3891a4b4f6ce3f3f90505061614fb7e65cf0135d0b664a50c3ed21eb18f3b7e",
    "cfg1_12gb": "fe0b0a2ece079b37c486e16a1338e5be667881b957c0c2681d15a94cfb60415e",
}
COPY_MASK_OFFSETS = (0x89C4, 0x89DA, 0x89F0, 0x8A06)
COPY_MASK_STOCK = (0x03, 0x0C, 0x30, 0xC0)
CHECKSUM_BYTE = 0xF3FF


def sha256(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def differences(a: bytes, b: bytes) -> list[int]:
    if len(a) != len(b):
        raise AssertionError(f"ROM size differs: {len(a)} != {len(b)}")
    return [i for i, (x, y) in enumerate(zip(a, b)) if x != y]


def m_regions(blob: bytes) -> dict:
    bit, entry, version, length, data = bit_m(blob)
    if version != 2 or length < 7:
        raise AssertionError(f"unexpected BIT M v{version}, length {length}")
    groups = blob[data]
    xlat = int.from_bytes(blob[data + 1:data + 3], "little")
    m0203 = int.from_bytes(blob[data + 3:data + 5], "little")
    header_len, entry_len, entry_count = blob[m0203 + 1:m0203 + 4]
    m0203_end = m0203 + header_len + entry_len * entry_count
    return {
        "bit_offset": bit,
        "entry_offset": entry,
        "entry": blob[entry:entry + 6],
        "data_offset": data,
        "data": blob[data:data + length],
        "groups": groups,
        "xlat_offset": xlat,
        "xlat": blob[xlat:xlat + 16],
        "m0203_offset": m0203,
        "m0203": blob[m0203:m0203_end],
    }


def cfg1_density_offsets(stock_path: Path) -> set[int]:
    parsed = decode(stock_path)
    offsets: set[int] = set()
    for op in parsed["cfg1_groups"]:
        values = op["values"]
        for pos, value in zip(op["value_offsets"], values):
            # 0x02559000 -> 0x02669000 differs only in the second byte (55->66).
            if int(value, 16) == 0x02559000:
                offsets.add(int(pos, 16) + 2)
    if len(offsets) != 32:
        raise AssertionError(f"expected 32 selected density bytes, got {len(offsets)}")
    return offsets


def validate(directory: Path) -> dict:
    paths = {key: directory / name for key, name in ROM_NAMES.items()}
    blobs = {key: path.read_bytes() for key, path in paths.items()}
    stock = blobs["stock"]
    density = cfg1_density_offsets(paths["stock"])
    expected_diffs = {
        "nofbio": {COPY_MASK_OFFSETS[0], CHECKSUM_BYTE},
        "s4": set(COPY_MASK_OFFSETS) | density | {CHECKSUM_BYTE},
        "cfg1_12gb": density | {CHECKSUM_BYTE},
    }

    stock_m = m_regions(stock)
    images = {}
    for key, blob in blobs.items():
        digest = sha256(blob)
        if digest != EXPECTED_SHA256[key]:
            raise AssertionError(f"{key}: SHA-256 mismatch: {digest}")
        if sum(blob[:FIRST_IMAGE_END]) & 0xFF:
            raise AssertionError(f"{key}: first-image sum8 is non-zero")
        region = m_regions(blob)
        for field in ("entry_offset", "entry", "data_offset", "data",
                      "groups", "xlat_offset", "xlat", "m0203_offset", "m0203"):
            if region[field] != stock_m[field]:
                raise AssertionError(f"{key}: BIT M/M0203 field changed: {field}")
        diffs = differences(stock, blob)
        if key != "stock" and set(diffs) != expected_diffs[key]:
            raise AssertionError(
                f"{key}: unexpected diff set; extra={sorted(set(diffs)-expected_diffs[key])}, "
                f"missing={sorted(expected_diffs[key]-set(diffs))}"
            )
        images[key] = {
            "file": str(paths[key]),
            "size": len(blob),
            "sha256": digest,
            "first_image_sum8": sum(blob[:FIRST_IMAGE_END]) & 0xFF,
            "diff_count_vs_stock": len(diffs),
            "diffs_vs_stock": [f"0x{x:x}" for x in diffs],
            "bit_m_entry_offset": f"0x{region['entry_offset']:x}",
            "bit_m_data_offset": f"0x{region['data_offset']:x}",
            "xlat_offset": f"0x{region['xlat_offset']:x}",
            "m0203_offset": f"0x{region['m0203_offset']:x}",
            "m0203_length": len(region["m0203"]),
        }

    for off, expected in zip(COPY_MASK_OFFSETS, COPY_MASK_STOCK):
        if stock[off] != expected:
            raise AssertionError(f"stock COPY_NV_REG mask {off:#x}: {stock[off]:#x} != {expected:#x}")
    if any(blobs["s4"][off] != 0 for off in COPY_MASK_OFFSETS):
        raise AssertionError("s4 did not clear all four COPY_NV_REG masks")
    if blobs["nofbio"][COPY_MASK_OFFSETS[0]] != 0:
        raise AssertionError("nofbio did not clear first COPY_NV_REG mask")
    if any(blobs["cfg1_12gb"][off] != stock[off] for off in COPY_MASK_OFFSETS):
        raise AssertionError("cfg1_12gb unexpectedly changes COPY_NV_REG")
    for key in ("s4", "cfg1_12gb"):
        if any(stock[o] != 0x55 or blobs[key][o] != 0x66 for o in density):
            raise AssertionError(f"{key}: a CFG1 density edit is not 55->66")

    return {
        "schema": 1,
        "result": "PASS",
        "scope": "offline-only; no hardware access",
        "first_image_end_exclusive": hex(FIRST_IMAGE_END),
        "cfg1_density_byte_count": len(density),
        "cfg1_density_offsets": [f"0x{x:x}" for x in sorted(density)],
        "copy_nv_reg_mask_offsets": [f"0x{x:x}" for x in COPY_MASK_OFFSETS],
        "images": images,
    }


def self_test(directory: Path) -> dict:
    validate(directory)
    stock = bytearray((directory / ROM_NAMES["stock"]).read_bytes())
    _, entry, _, _, _ = bit_m(stock)
    stock[entry + 4] ^= 0x01  # corrupt the BIT M data pointer in memory only
    try:
        region = m_regions(bytes(stock))
        original = m_regions((directory / ROM_NAMES["stock"]).read_bytes())
        if region["data_offset"] == original["data_offset"]:
            raise AssertionError("mutant did not alter pointer")
        # The full validator is path-based, so exercise the same invariant here.
        if region["data"] != original["data"]:
            raise AssertionError("expected rejection: BIT M data differs")
    except (AssertionError, ValueError, IndexError) as exc:
        return {"result": "PASS", "mutant": "BIT M pointer +1", "rejected_as": str(exc)}
    raise AssertionError("corrupted in-memory mutant was accepted")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("directory", nargs="?", default="volatile_rom_audit", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    result = validate(args.directory)
    if args.self_test:
        result["self_test"] = self_test(args.directory)
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
