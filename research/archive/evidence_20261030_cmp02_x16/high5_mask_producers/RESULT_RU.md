> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Bounded high5 mask/shift candidates: RM580 + MODS367

## Result

This pass did not establish a new GP100 high5 writer. It searched for value-shaping candidates that do not require a literal `0x8c040`: raw DWORDs `0xf8000000`, `0x07ffffff`, `0x1f`, plus exact x86 `C1 /4, imm8=27` SHL encodings; then decoded only containing FDEs (or sized RM symbols where the `.ko` supplies no usable FDE table).

| Input | Raw DWORD counts: `f8000000 / 07ffffff / 1f` | raw shift27 encodings | candidate owners decoded | Confirmed owner sets |
|---|---:|---:|---:|---:|
| RM580 | 9 / 20 / 10,135 | 38 | 2,000 function owners (cap) | 1,961 |
| MODS367 | 178 / 268 / 3,163 | 88 | 2,755 FDEs | 2,630 |

These counts include many unrelated signedness/field-width uses of `0x1f` and shifts by 31. The confirmed set is an instruction-candidate inventory, not a finding that each operation concerns PCIe width.

## Relevant candidates and what they establish

- RM580 `0x957760..0x957b6f` (`_nv051302rm`) and `0x95c5f0..0x95c9df` (`_nv051299rm`) each combine `AND 0x07ffffff` with `SHL 27`. These are the already-known record reconstruction paths: value comes from `8c080[4:0]` plus `8c040[26:0]` and is stored in a record/output buffer, not demonstrated as a register write. This corroborates the existing RM580 literal audit (archive reference: `../rm580_maxlane/RESULT_RU.md`; original context may be excluded); it is not a new writer.
- MODS367 `0x150fca0..0x15100ee` is the already identified `pgislandPatchPrivRestoreState_GP102` candidate; `AND 0x07ffffff` + `SHL 27` builds the restore record. It is GP102-named, not GP100 writer evidence.
- MODS367 also has mixed high/low masks in FDEs such as `0x5a2240..0x5a2aae`, `0x1df0a4d..0x1df15b5`, and `0x2613a14..0x26140af`. This pass proves the immediate instructions and owner FDEs, but does not resolve their register provenance, destination class, or GP100 HAL binding. Their raw decoded windows are preserved in MODS367 candidates (archive reference: `mods367_candidate_fdes.disasm.txt`; original context may be excluded).

Accordingly the inventory yields no positive high5 writer path. The precise remaining edge is a GP100-bound operation that takes a candidate high5 value through an identified GPU register-write sink; neither a mask/shift pair nor a restore-record constructor closes that edge. No new claim is made about all computed forms or all indirect sinks.

## Reproduction and limits

`python3 evidence_20261030_cmp02_x16/high5_mask_producers/audit_high5_masks.py` → `PASS_BOUNDED_MASK_CANDIDATES`.

Input pins: RM580 `ea81a7018398a59c53299c80c9b80276b45405d24f566e66f32430cf9bcb1237`; MODS367 `74daa3f6237d7fe88f4be102ae1f638b8d95e91d731f0eda09d1a991f9f820cb`. Machine-readable counts and every confirmed candidate instruction are in AUDIT.json (archive reference: `AUDIT.json`; original context may be excluded). RM580 has no usable DWARF FDE table, so sized `.text` STT_FUNC owners are used and the `0x1f` family is capped at 2,000 unique owners; 6,759 lower-priority owner encounters fell beyond that cap. The high5, low27, and shift27 candidates are prioritized ahead of that cap. MODS367 uses its parsed FDEs. The exact raw `0x1f` count is complete in both inputs, but its decoded context is intentionally bounded for RM580. Only offline file reads/disassembly were used.

Parent review corrected the raw SHL candidate scan: inspect the ModRM after C1, independent of an optional REX prefix, and decode displacement/SIB before locating imm8. Raw shift candidate counts above include this correction. ELF type metadata now uses e_type rather than e_machine. The added candidates remain unbound unless separately traced; a bit27-only helper at MODS1590d60 is under separate caller audit.
