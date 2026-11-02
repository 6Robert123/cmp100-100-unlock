"""Offline structural comparison of three existing NVGI images; no hardware access.

Uses the payload-count encoding inferred and boundary-checked on three images.
Byte coverage is not proof of hardware execution or every flag's semantics.
"""
import difflib
import hashlib
import json
import struct
from pathlib import Path

INPUTS = {
    'CMP_FACTORY': '/LOCAL-USER/Downloads/CMP100-CH341A/RECOVERY_FACTORY_FULL_512K.bin',
    'P100': '/LOCAL-USER/orca/projects/cmp100-100/reference_roms/dell_p100/p100-16gb-86.00.41.00.06.nvgi',
    'QUADRO': '/LOCAL-USER/orca/projects/cmp100-100/reference_roms/quadro_gp100/quadro-gp100-16gb-86.00.3A.00.01.rom',
}
FACTORY_SHA = '96467addaa72dfb181c6c0452a819bfa79c3ca20958ae26d13c4b74e9ca096ef'

def parse(path):
    data = Path(path).read_bytes()
    assert data[:4] == b'NVGI'
    end = struct.unpack_from('<I', data, 8)[0] & 0xffffff
    assert 24 <= end <= len(data)
    offset = 24
    records, spans, pieces, opaque_sections = [], [], [data[:24]], []
    while offset < end:
        assert offset + 4 <= end
        header = struct.unpack_from('<I', data, offset)[0]
        if header == 0:
            start = offset
            while offset + 4 <= end and data[offset:offset+4] == bytes(4):
                offset += 4
            spans.append([hex(start), hex(offset)])
            pieces.append(data[start:offset])
            continue
        typ = header >> 24
        # Derived from all three images: bits 6:1 encode payload word count-1.
        # This handles 0x10 (9 words), 0x7e (64), 0xca (38), 0x1e (16).
        # Do not infer RMW semantics from count bits; retain address flags.
        count = ((typ & 0x7e) >> 1) + 1
        size = 4 * (1 + count)
        assert offset + size <= end
        raw = data[offset:offset+size]
        words = struct.unpack('<' + 'I'*(size//4), raw)
        records.append({'offset': hex(offset), 'address': hex(header & 0xfffffc),
                        'type': hex(typ), 'payload_word_count': count,
                        'address_flags': header & 3,
                        'operands': [hex(v) for v in words[1:]], 'raw': raw.hex()})
        pieces.append(raw)
        offset += size
    opaque = data[offset:end]
    pieces.append(opaque)
    assert b''.join(pieces) == data[:end]
    return {'path': path, 'sha256': hashlib.sha256(data).hexdigest(),
            'image_bytes': len(data), 'ifr_end': hex(end),
            'ifr_byte_sum_mod256': sum(data[:end]) % 256,
            'structural_roundtrip': True, 'zero_spans': spans,
            'opaque_sections': opaque_sections,
            'opaque_tail_offset': hex(offset), 'opaque_tail_bytes': len(opaque),
            'opaque_tail_sha256': hashlib.sha256(opaque).hexdigest(),
            'opaque_tail_hex': opaque.hex(),
            'decode_complete': offset == end,
            'record_count': len(records), 'records': records}

def signature(r):
    return (r['address'], r['type'], r['address_flags'], tuple(r['operands']))

def main():
    results = {name: parse(path) for name, path in INPUTS.items()}
    assert results['CMP_FACTORY']['sha256'] == FACTORY_SHA
    diffs = {}
    firmware = {}
    for name, result in results.items():
        recs = result['records']
        desc = next(r for r in recs if r['address'] == '0x10a1c4' and r['payload_word_count'] == 9)
        raw = bytes.fromhex(desc['raw'])[4:]
        code = b''.join(struct.pack('<I',int(w,16)) for r in recs
                        if r['address'] == '0x10a184' and r['payload_word_count'] > 1
                        for w in r['operands'])
        code_src = struct.unpack_from('<I',raw,8)[0]
        code_dst, code_size = struct.unpack_from('<HH',raw,12)
        secret_start, secret_end = struct.unpack_from('<HH',raw,16)
        data_src = struct.unpack_from('<I',raw,20)[0]
        data_dst, data_size = struct.unpack_from('<HH',raw,24)
        firmware[name] = {'loader_bytes':len(code), 'loader_sha256':hashlib.sha256(code).hexdigest(),
                          'descriptor_words':desc['operands'],
                          'code_source_flagged':hex(code_src), 'code_destination':hex(code_dst),
                          'code_size':hex(code_size), 'secret_interval':[hex(secret_start),hex(secret_end)],
                          'data_source_flagged':hex(data_src), 'data_destination':hex(data_dst),
                          'data_size':hex(data_size),
                          'code_end_equals_data_start':(code_src&0x7fffffff)+code_size==(data_src&0x7fffffff)}
    for name in ('P100', 'QUADRO'):
        a, b = results['CMP_FACTORY']['records'], results[name]['records']
        matcher = difflib.SequenceMatcher(None, list(map(signature,a)), list(map(signature,b)), autojunk=False)
        diffs[name] = [{'operation': tag, 'CMP': a[i:j], name: b[k:l]}
                       for tag,i,j,k,l in matcher.get_opcodes() if tag != 'equal']
    out = Path(__file__).with_name('ifr_compare_20260929.json')
    out.write_text(json.dumps({'scope':'offline structural comparison, not hardware execution proof',
                               'images':results, 'differences':diffs,
                               'firmware_payloads':firmware}, indent=2)+'\n')
    for name,r in results.items():
        print(name, r['record_count'], r['ifr_end'], 'roundtrip=PASS')
    for name, changes in diffs.items():
        print('DIFF',name)
        for change in changes:
            print(json.dumps(change))

if __name__ == '__main__':
    main()
