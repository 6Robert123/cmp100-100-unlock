> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# CMP SPI identity join: Oct 26 CH341 inputs vs current target (offline)

## Result

The Oct 26 CH341 intake contains three exact-image groups. Each raw OBD serial was checked against a preserved per-device primary OEM GET run: its saved stdout includes requested UUID/BDF and payload bytes, and its execution receipt repeats the requested UUID/BDF. I recomputed each payload hash and checked the serial bytes in the payload. The Oct 26 read-only runtime inventory independently maps UUID to BDF and product. The derived audit JSON is only a cross-check, not the identity source:

| Oct 26 SPI group | SHA-256 | OBD serial | OEM GET identity |
|---|---|---|---|
| `Cmp100-100_2*` (3 byte-identical reads) | `8ae75ee4d1fc28e7c5e42982d16f726f40017b731ac046474e5207e82bb406ba` | `BOARD-SERIAL-REDACTED` | CMP03, UUID `GPU-REDACTED`, BDF `0000:03:00.0` |
| `Cmp100-100_3*` (3 byte-identical reads) | `3e25541f9af0c536b681755761e130ce84016debe0317815bddfd7de36d881a2` | `BOARD-SERIAL-REDACTED` | CMP01, UUID `GPU-REDACTED`, BDF `0000:01:00.0` |
| `p100_*` (2 byte-identical reads) | `e687e66ca234663acfa94b013b5ed7169d261c81e096d9feb5bc1461457cde34` | `BOARD-SERIAL-REDACTED` | P100, UUID `GPU-REDACTED`, BDF `0000:02:00.0` |

I re-read the raw OBD-shaped records at the offsets in the reviewed October intake metadata and checked their bytes and serial fields directly. The two P100 OBD prefixes at file offsets `225680` and `258192` have the same serial and 112-byte hash. Each CMP group has one such candidate. The CMP prefixes carry zero in the saved memory manufacturer/part/date fields. This is OBD metadata, not a verified active-directory/checksum chain or a physical HBM ID.

The Oct 29 target acquisition contains its own same-run identity chain: the preflight event in `run.stdout` records UUID `GPU-REDACTED`, serial `BOARD-SERIAL-REDACTED`, BDF `00000000:02:00.0`, and 12,288 MiB at Gen1 x1. The same log pins NVFlash index 1 to bus 2; NVFlash reports device `10DE:15C2`, subsystem `10DE:12B4` at bus 02. Both saved 524,288-byte software reads have the same SHA-256 `74f2eba8dcf8d1ed14922a34493ddafa013830bcc1635af6c47950fe75a6e878`; the complete OBD record at `0x38190` carries serial `BOARD-SERIAL-REDACTED`, matching that same-run preflight. The separate Oct 29 runtime CSV also agrees. The Oct 26 inventory is a different source and contains only UUID/BDF/product/configured memory/link, no serial column; it is represented separately in the JSON.

Therefore **none of the Oct 26 CH341 input groups is an SPI read for the Oct 29 current target**. The target’s retained image is a software NVFlash read with a same-run serial/UUID/BDF join; it is not an independent clip read. The October run’s overall `run.meta.json` return code is 1 because restore/recovery did not complete. This identity result does not assert target health based on that run.

## Relevance and limits

This closes a source-assignment ambiguity: do not use the Oct 26 CMP01/CMP03 clip images as the current target’s per-card physical ROM evidence. It adds no new information about HBM density, 8 GiB per stack, physical presence of the fourth site, or an enable path. Equal OBD serials identify the board metadata represented in an image; they do not prove untouched factory contents or physical SPI-chip coverage. Configured 12/16 GiB, RAM geometry, masks, and NVMT’s arithmetic `HBM DENSITY` output remain distinct from a valid physical Device ID.

## Reproduction and source pins

The checker `audit_cmp_spi_identity_join.py` reads only the preserved raw files and existing receipts. It does not run archived parsers/builders, touch originals, access SSH, or access GPU hardware. Its structured output is `CMP_SPI_RUNTIME_IDENTITY_JOIN.json`.

- Oct 26 raw-input manifest, metadata candidate offsets, and runtime inventory: `../../evidence_20261026_spi_intake/MANIFEST.json`, `../../evidence_20261026_spi_intake/START_HERE_RU.md`, `../../evidence_20261026_spi_intake/decoded/memory_metadata_comparison.json`, and `../../evidence_20261026_spi_intake/lab_readonly_identity.txt`.
- Primary Oct 26 per-device GET receipts: `../../evidence_20261022_photo_followup/rm580_oem_board_info_candidate/CMP01.stdout.txt`, `CMP03.stdout.txt`, `P100.stdout.txt`, matching `*.execution.json` files, and the corresponding `*.postcheck.json` files. The `OEM_BOARD_INFO_AUDIT_20261026.json` in that same directory is used as a derived cross-check only.
- Oct 29 same-run target and reads: `../../evidence_20261029_cmp02_x16/max_lane_producer/current_target_spi_read/run.stdout`, `run.meta.json`, `inputs/cmp02-full-spi-read-1.bin`, `inputs/cmp02-full-spi-read-2.bin`, and `analysis/CURRENT_CMP02_SPI_ANALYSIS.json`.
- No original ROM, session log, or prior report was modified. This is an additive cross-time identity join, not a repeat of the Oct 26 intake’s byte-lineage audit.
