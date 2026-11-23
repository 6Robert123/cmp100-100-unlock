# Stock VBIOS dumps

Untouched original firmware images, byte-for-byte as they were read. No patches, no edits — handy as a reference, for diffing, or to bring a card back to factory state.

| File | Card | VBIOS | Size | How it was read |
|---|---|---|---|---|
| `cmp100-100_86.00.50.00.01_card1_spi.bin` | CMP 100-100 (card 1) | 86.00.50.00.01 | 512 KiB | full SPI dump, programmer |
| `cmp100-100_86.00.50.00.01_card2_spi.bin` | CMP 100-100 (card 2) | 86.00.50.00.01 | 512 KiB | full SPI dump, programmer |
| `cmp100-100_86.00.50.00.01_card3_spi.bin` | CMP 100-100 (card 3) | 86.00.50.00.01 | 512 KiB | full SPI dump, programmer |
| `tesla-p100-pcie-16gb_86.00.41.00.06_spi.bin` | Tesla P100-PCIE-16GB | 86.00.41.00.06 | 512 KiB | full SPI dump |
| `tesla-p100-pcie-12gb_86.00.41.00.07.rom` | Tesla P100-PCIE-12GB | 86.00.41.00.07 | 266.5 KiB | ROM image |

The three CMP dumps are identical except for the board info block around offset `0x38188`: board serial number, build byte and that block's checksum. Everything else matches bit for bit.

Checksums are in `SHA256SUMS`.

⚠️ Flash at your own risk. Always dump your own card first and keep that file safe — a wrong image or a bad flash can brick the board. Firmware is © NVIDIA; it's shared here only for research, compatibility and recovery.

---

**RU:** Оригинальные прошивки без каких-либо правок — три CMP 100-100 (полные SPI-дампы) и две Tesla P100 (16 и 12 ГБ). Дампы CMP отличаются только блоком с серийником платы около `0x38188`. Перед прошивкой обязательно снимите свой дамп, шьёте на свой страх и риск.
