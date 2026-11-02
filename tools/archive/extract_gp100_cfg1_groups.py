#!/usr/bin/env python3
"""Offline extractor for GP100 DevInit opcode 0x8f CFG1 groups."""
import argparse, hashlib, json
from pathlib import Path

TARGETS = [0x900204 + i * 0x4000 for i in range(16)]

def hx(n): return f"0x{n:x}"

def bit_m(blob):
    sig = blob.find(b"BIT\0")
    if sig < 2:
        raise ValueError("BIT signature not found")
    base = sig - 2                 # nouveau bios->bit_offset convention
    rlen, count = blob[base + 9], blob[base + 10]
    for i in range(count):
        off = base + 12 + i * rlen
        if blob[off] == ord("M"):
            ver = blob[off + 1]
            length = int.from_bytes(blob[off+2:off+4], "little")
            data = int.from_bytes(blob[off+4:off+6], "little")
            return base, off, ver, length, data
    raise ValueError("BIT M entry not found")

def decode(path):
    b = path.read_bytes()
    bit, mrec, mver, mlen, moff = bit_m(b)
    if mver != 2 or mlen < 7:
        raise ValueError("expected BIT M v2 length >= 7")
    groups = b[moff]
    xlat = int.from_bytes(b[moff+1:moff+3], "little")
    m0203 = int.from_bytes(b[moff+3:moff+5], "little")
    tv, th, elen, ecnt, typ = b[m0203:m0203+5]
    mapping = []
    for i in range(ecnt):
        o = m0203 + th + i * elen
        mapping.append({"entry": i, "rom_offset": hx(o),
                        "strap": b[o] >> 4, "entry_type": b[o] & 0xf,
                        "group": b[o+1] & 0xf, "raw": b[o:o+elen].hex()})
    ops = []
    for target_i, target in enumerate(TARGETS):
        needle = b"\x8f" + target.to_bytes(4, "little") + b"\x04\x01"
        found = []
        p = 0
        while True:
            p = b.find(needle, p)
            if p < 0: break
            found.append(p); p += 1
        if len(found) != 1:
            raise ValueError(f"target {target:#x}: expected one opcode, got {found}")
        p = found[0]; vals = [int.from_bytes(b[p+7+4*j:p+11+4*j], "little") for j in range(groups)]
        ops.append({"fbpa": target_i, "opcode_offset": hx(p), "opcode": "0x8f",
                    "target": hx(target), "increment": 4, "register_count": 1,
                    "value_offsets": [hx(p+7+4*j) for j in range(groups)],
                    "values": [hx(v) for v in vals],
                    "context_before": b[max(0,p-16):p].hex(),
                    "context_after": b[p+7+4*groups:p+7+4*groups+16].hex()})
    strap0 = next((x for x in mapping if x["strap"] == 0), None)
    # Constrained scan matches the 0x8f records printed by envytools for these
    # images: GP100 MMIO address, dword increment, 1..8 target registers.
    all_groups = []
    for p, opcode in enumerate(b):
        if opcode != 0x8f or p + 7 + 4 * groups > len(b): continue
        addr = int.from_bytes(b[p+1:p+5], "little")
        incr, num = b[p+5], b[p+6]
        if addr >= 0x02000000 or incr != 4 or not (1 <= num <= 8): continue
        vals = [int.from_bytes(b[p+7+4*j:p+11+4*j], "little") for j in range(groups)]
        all_groups.append({"opcode_offset": hx(p), "target": hx(addr),
                           "register_count": num, "index0": hx(vals[0]),
                           "index1": hx(vals[1]), "index0_ne_index1": vals[0] != vals[1]})
    return {
      "file": str(path), "size": len(b), "sha256": hashlib.sha256(b).hexdigest(),
      "bit": {"offset": hx(bit), "M_entry_offset": hx(mrec), "M_version": mver,
              "M_length": mlen, "M_data_offset": hx(moff),
              "ram_restrict_group_count": groups, "xlat_offset": hx(xlat),
              "xlat_first16": list(b[xlat:xlat+16]), "M0203_offset": hx(m0203),
              "M0203_version": tv, "M0203_header_length": th,
              "M0203_entry_length": elen, "M0203_entry_count": ecnt,
              "M0203_type": typ, "strap_mapping": mapping,
              "strap0_selected_group": strap0["group"] if strap0 else None},
      "cfg1_opcode_span": {"start": ops[0]["opcode_offset"],
                            "end_exclusive": hx(int(ops[-1]["opcode_offset"],16)+7+4*groups)},
      "cfg1_groups": ops,
      "all_opcode_8f_summary": {
          "constrained_record_count": len(all_groups),
          "index0_ne_index1_count": sum(x["index0_ne_index1"] for x in all_groups),
          "differences": [x for x in all_groups if x["index0_ne_index1"]]},
      "literal_counts": {
          hx(v): sum(x == v for op in ops for x in map(lambda s:int(s,16), op["values"]))
          for v in sorted({int(s,16) for op in ops for s in op["values"]})},
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", nargs="+")
    ap.add_argument("-o", "--output")
    a = ap.parse_args()
    out = {"schema": 1, "description": "GP100 opcode 0x8f CFG1 alternatives", 
           "roms": [decode(Path(x)) for x in a.rom]}
    text = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
    if a.output: Path(a.output).write_text(text)
    else: print(text, end="")

if __name__ == "__main__": main()
