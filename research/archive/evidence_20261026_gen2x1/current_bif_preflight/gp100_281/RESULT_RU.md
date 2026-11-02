> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# GP100 `_nv017281rm`: offline producer audit BIF+954/+955

`audit_gp100_281_flags.py` (archive reference: `audit_gp100_281_flags.py`; original context may be excluded) re-decodes `.text` VA `0x12c050..0x12c7af` from the pinned ELF and checks the key branch/store bytes. Replay result is `PASS` in `audit.json` (archive reference: `audit.json`; original context may be excluded). This follows the statically selected GP100 ordinal-29 path `_nv005948rm → _nv017282rm → _nv017281rm` documented by the dispatch correction (archive reference: `../../../evidence_20261022_photo_followup/rm_pcie_speed_cmp03/RM580_GEN2_DISPATCH_CORRECTION.txt`; original context may be excluded). No runtime call-edge claim is made.

## Stores and dataflow

The function has exactly three decoded direct flag stores:

- `0x12c1e0`: `movw %ax,+0x954(%r12)` after `xor eax,eax`; writes `+954/+955=0/0`.
- `0x12c459`: `movb %r15b,+0x954(%r12)`.
- `0x12c469`: `movb %bl,+0x955(%r12)`.

At entry, `0x88610` is read through `_nv053907rm`; bit 0 becomes `r15d`/`r15b` (+954 candidate), and bit 12 becomes EBX/BL (+955 candidate). With `BIF+0xa34 != 0`, `+0xa38` is decoded before the `0x8c1c0` read. For `a38=5`, fields `[1:0]=1` and `[3:2]=1`; separate blocks set `r15d=1` at `0x12c75c` and `ebx=1` at `0x12c6dc`, regardless of zero input candidates from `0x88610`.

Before reading `0x8c1c0`, BIF `+0x959` and `+0x95a` can clear the +954 candidate: nonzero `+0x959` reaches `0x12c3dc` (`xor r15d,r15d`); if `+0x959==0` but `+0x95a!=0`, `0x12c3ff` also clears `r15d`. The indirect callback at `OBJBIF+0x618` can clear EBX at `0x12c178`, affecting +955 only; it does not clear `r15d`.

The function reads `0x8c1c0` and computes `(value >> 20) & 0xf`:

| nibble | resulting flag path |
|---|---|
| 0 | stores `r15b` to +954 and `bl` to +955 |
| 1 | sets EBX=0, then stores `r15b` to +954 and 0 to +955 |
| 2 | unconditional zero-word store at `0x12c1e0` |
| 3–15 | diagnostic path then the same zero-word block |

Thus for the V3 values `a34/a38=1/5`, **if this callback is the writer**, `+954/+955=0/0` follows directly when nibble is 2–15. If nibble is 0/1, +954 stays 1 unless `+0x959` or `+0x95a` cleared `r15d`; nibble 0 also needs the independent +955 candidate cleared, e.g. nonzero return from `+0x618`. Later code changes CYA/BAR state, but those operations do not produce these bytes.

## Archived phase comparison (not same-instant causality)

Bounded replay in `compare_historical_phases.py` (archive reference: `compare_historical_phases.py`; original context may be excluded) reads only the archived CMP03 snapshots from `evidence_20261022_photo_followup/cmp03_88610_set2_observed_20261023T/cmp03-global5-cya-tls2-rl-stage2-20261023T040044Z/`; it passes checks recorded in `historical_phases.json` (archive reference: `historical_phases.json`; original context may be excluded). All rows identify CMP03 UUID `GPU-REDACTED` where a BIF snapshot exists.

| Exact phase/file | Monotonic timestamp(s) | Observed values | SHA-256 |
|---|---:|---|---|
| `global5_pre_unbind.json` | 61283224278549 ns | BAR snapshot: `8c1c0=0x00210036` (nibble 2), `88610=0`, `8841c=0xc0b46500`; this file does **not** include BIF +954/+955 | `104e52953d903bcb339ca3922c39a76e201f6fe68d8206ee069c1d24c6d87756` |
| `post_recovery.json` | 61297292295506 ns | BAR: `8c1c0=0x00120036` (nibble 1), `88610=0`, `8841c=0xc0b44500` | `b9416ef4fc27a67caba5a724f1963ac803042b20d9f40a4be314cef31bea407f` |
| `first-gate-global5-postbind.json` | 61298571828342–61298572446873 ns | kcore BIF: `a34/a38=1/5`, `+954/+955=1/0`, `pre_set_mode6_call=true`; nearest post-recovery BAR row is 1.280 s earlier | `fa639b2747f8ca830abf9095ba620d6c07971c9f47b1e7582e311fac7349230b` |
| `root_tls2_pre_cache.json` | 61299486020708 ns | BAR: nibble 1, `88610=0`, `8841c=0xc0b44500` | `71aacdb9c28ced66c1987094069d74a9eecc2210da0d5fe617923c2d6a4a3496` |
| `first-gate-root-tls2-pre-set2.json` | 61300660126218–61300660752501 ns | kcore BIF: `a34/a38=1/5`, `+954/+955=1/0`, `pre_set_mode6_call=true`; bracketed by the nibble-1 BAR snapshots 1.174 s before and 0.280 s after | `0cd3452934fefbee9b03116b30b21afd0f53d5ce301a4316245f0524f52c8e86` |
| `root_tls2_post_cache.json` | 61300940303779 ns | BAR: nibble 1, `88610=0`, `8841c=0xc0b44500` | `20cac993e59631c74da5a6a0d2bc01fbc3fdd6459eb48ec0bdb9218818d132e0` |
| `pre_native_set2-bar-before.json` | 61304847132148 ns | BAR: nibble 1, `88610=0x1001`, `8841c=0x00b40500` | `0c2661105d902e1ef22dfb10728000fd7ec1ac5c918fa259f364fb90c5d1a0de` |
| `pre_native_set2-kcore.json` | 61306015207612–61306016673269 ns | kcore BIF: `a34/a38=1/5`, `+954/+955=1/0`; BAR before and after same-phase check kept nibble 1 | `68f9852046575de431c11db1d9c08a1edc48e14d0a56dac9c5daa85ed703990e` |

This is a useful historical mechanism match: the static 281 branch says nibble 2 directly zeros the word, while nibble 1 stores the `a38=5` Gen2 candidate as `+954=1` and forces `+955=0`. The snapshots show a change from nibble 2 before unbind to nibble 1 in later recovered/pre-SET phases, with `+954/+955=1/0` observed in adjacent kcore windows. They do **not** prove the earlier unbind phase had BIF flags 0/0 (not captured there), nor prove the same callback wrote the later bytes; reset/recovery also changed CYA/DI state. Run log pin: SHA-256 `688e9fd052ac936d87d32f21f0733aa1ef547cea005dfd75e482c8dcbfed5589`, exact phase lines 16, 168, 186, 189, 221 and same-phase check line 227.

## Next observation

The archived `pre_native_set2` state is a known historical baseline immediately before its native SET2 observation; it is not a newly inferred universal prerequisite. The next useful step is to observe the already planned SET call under that prepared baseline and capture the relevant actual read/write path. Do not repeat the global5 setup to recreate this comparison or invent additional setup requirements from it. The newer V3 kcore state still omits `0x8c1c0`, `+0x959`, and `+0x95a`, so it cannot explain its `0/0` values retrospectively.

## Replay and pin

```sh
python3 evidence_20261026_gen2x1/current_bif_preflight/gp100_281/audit_gp100_281_flags.py > evidence_20261026_gen2x1/current_bif_preflight/gp100_281/audit.json
python3 evidence_20261026_gen2x1/current_bif_preflight/gp100_281/compare_historical_phases.py > evidence_20261026_gen2x1/current_bif_preflight/gp100_281/historical_phases.json
```

ELF: `evidence_20261022_photo_followup/rm580_generic_offline/nvidia580-analysis.ko`, SHA-256 `ea81a7018398a59c53299c80c9b80276b45405d24f566e66f32430cf9bcb1237`. Audit output is limited to this function's direct stores and the cited upstream branches; it cannot rule out later writers outside this function.
