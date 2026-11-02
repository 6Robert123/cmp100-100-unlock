#!/usr/bin/env python3
"""Bounded read-only parser for the BIT-p Falcon table in two local ROMs.

Implements only the image/table and PMU-record fields used by Nouveau v6.12
nvbios_pmuRm. It does not decompress, execute, or modify firmware bytes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


DEFAULT_IMAGES = [
    Path("memory_table_assets/cmp100-stock-86.00.50.00.01.rom"),
    Path("memory_table_assets/p100-16gb-86.00.41.00.06-full.rom"),
]


def u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def bounds(buf: bytes, off: int, size: int, label: str) -> bytes:
    if off < 0 or size < 0 or off + size > len(buf):
        raise ValueError(f"{label} outside ROM: [{off:#x}, {off + size:#x}) / {len(buf):#x}")
    return buf[off : off + size]


def parse_images(buf: bytes) -> list[dict]:
    images = []
    off = 0
    while off + 0x1A <= len(buf):
        if buf[off : off + 2] != b"\x55\xaa":
            break
        pcir_rel = u16(buf, off + 0x18)
        pcir = off + pcir_rel
        if bounds(buf, pcir, 0x18, "PCIR header")[:4] != b"PCIR":
            raise ValueError(f"invalid PCIR signature at {pcir:#x}")
        blocks = u16(buf, pcir + 0x10)
        size = blocks * 512
        code_type = buf[pcir + 0x14]
        indicator = buf[pcir + 0x15]
        bounds(buf, off, size, "PCIR image")
        images.append({"offset": off, "size": size, "end": off + size,
                       "code_type": code_type, "indicator": indicator})
        off += size
        if indicator & 0x80:
            break
    if not images or not (images[-1]["indicator"] & 0x80):
        raise ValueError("PCIR image chain did not reach a last-image flag")
    return images


def parse_one(path: Path) -> dict:
    buf = path.read_bytes()
    if buf[:2] != b"\x55\xaa":
        raise ValueError(f"{path}: missing PCI option-ROM signature")
    images = parse_images(buf)
    x86 = next((x for x in images if x["code_type"] == 0), None)
    efi = next((x for x in images if x["code_type"] == 3), None)
    if not x86 or not efi:
        raise ValueError(f"{path}: expected code-type 00 and 03 images")

    # BIT header = FF B8 'BIT\0'; header size/token size/count are byte fields.
    sig = b"\xff\xb8BIT\x00"
    bit = buf.find(sig)
    if bit < 0:
        raise ValueError(f"{path}: BIT signature not found")
    header_size, token_size, token_count = buf[bit + 8 : bit + 11]
    if token_size != 6:
        raise ValueError(f"{path}: unexpected BIT token size {token_size}")
    p_token = None
    for i in range(token_count):
        tok = bit + header_size + i * token_size
        ident, version, size, ptr = struct.unpack_from("<BBHH", buf, tok)
        if ident == ord("p"):
            p_token = {"offset": tok, "version": version, "size": size, "data_offset": ptr}
            break
    if not p_token or p_token["version"] != 2 or p_token["size"] < 4:
        raise ValueError(f"{path}: BIT p v2 token unavailable")

    # NVIDIA BIT spec: pointers past the code-type-00 image add following EFI size.
    bounds(buf, p_token["data_offset"], 4, "BIT p token data")
    table_pointer = u32(buf, p_token["data_offset"])
    p_token["table_pointer"] = table_pointer
    pointer_adjust = efi["size"] if table_pointer > x86["size"] else 0
    falcon_table = table_pointer + pointer_adjust
    ver, hdr_size, entry_size, count = struct.unpack_from("<4B", bounds(buf, falcon_table, 4, "Falcon table header"))
    if hdr_size < 4 or entry_size < 6 or count > 64:
        raise ValueError(f"{path}: implausible Falcon table header")
    bounds(buf, falcon_table, hdr_size + entry_size * count, "Falcon table")

    entries = []
    records = []
    all_ranges = []
    for i in range(count):
        ent = falcon_table + hdr_size + i * entry_size
        typ = buf[ent]
        stored = u32(buf, ent + 2)
        entry = {"index": i, "type": typ, "stored_pointer": stored}
        if not stored:
            entries.append(entry)
            continue
        desc = stored + pointer_adjust
        bounds(buf, desc, 0x30, f"entry {i} descriptor")
        entry["descriptor_offset"] = desc
        entry["descriptor_header_hex"] = buf[desc : desc + 0x30].hex()

        if typ in (0x01, 0x04):
            # Mirrors Nouveau v6.12 nvbios_pmuRm offsets and address arithmetic.
            init_addr_pmu = u32(buf, desc + 0x08)
            args_addr_pmu = u32(buf, desc + 0x0c)
            boot_addr_pmu = u32(buf, desc + 0x10) + u32(buf, desc + 0x18)
            boot_size = u32(buf, desc + 0x1c) - u32(buf, desc + 0x18)
            boot_addr = desc + 0x30
            code_addr = boot_addr + boot_size
            code_size = u32(buf, desc + 0x20)
            data_addr = desc + 0x30 + u32(buf, desc + 0x24)
            data_addr_pmu = u32(buf, desc + 0x28)
            data_size = u32(buf, desc + 0x2c)
            rec = {"entry_index": i, "type": typ,
                   "type_label": {1: "PRE_OS (gm200_devinit_preos)",
                                  4: "DEVINIT (gm200_devinit_post)"}[typ],
                   "descriptor_offset": desc,
                   "init_addr_pmu": init_addr_pmu, "args_addr_pmu": args_addr_pmu,
                   "boot_addr_pmu": boot_addr_pmu,
                   "code_addr_pmu": u32(buf, desc + 0x10) + u32(buf, desc + 0x18) + boot_size,
                   "data_addr_pmu": data_addr_pmu,
                   "segments": []}
            for name, start, size in (("boot", boot_addr, boot_size),
                                      ("code", code_addr, code_size),
                                      ("data", data_addr, data_size)):
                chunk = bounds(buf, start, size, f"entry {i} {name}")
                end = start + size
                seg = {"name": name, "start": start, "end_exclusive": end,
                       "size": size, "sha256": hashlib.sha256(chunk).hexdigest()}
                rec["segments"].append(seg)
                all_ranges.append((start, end, f"entry{i}.{name}"))
            records.append(rec)
            entry["record"] = rec
        else:
            # Keep unknown descriptor types opaque; do not infer their schema.
            entry["record_interpretation"] = "not decoded by bounded parser"
        entries.append(entry)

    ordered = sorted(all_ranges)
    overlaps = []
    for (a0, a1, an), (b0, b1, bn) in zip(ordered, ordered[1:]):
        if b0 < a1:
            overlaps.append({"left": an, "right": bn, "overlap_start": b0,
                             "overlap_end_exclusive": min(a1, b1)})
    if overlaps:
        raise ValueError(f"overlapping parsed segments: {overlaps}")

    return {"path": str(path), "size": len(buf), "sha256": hashlib.sha256(buf).hexdigest(),
            "pcir_images": images, "bit": {"offset": bit, "token": p_token},
            "efi_pointer_adjustment": pointer_adjust, "falcon_table": {
                "offset": falcon_table, "version": ver, "header_size": hdr_size,
                "entry_size": entry_size, "count": count, "entries": entries},
            "parsed_pmu_records": records, "segment_overlaps": overlaps}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="*", type=Path, default=DEFAULT_IMAGES)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()
    result = {"parser": "parse_gp100_rom_falcon_segments.py",
              "scope": "read-only BIT-p table and Nouveau PMU-record segment bounds; no decode/execute",
              "results": [parse_one(path) for path in args.images]}
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
