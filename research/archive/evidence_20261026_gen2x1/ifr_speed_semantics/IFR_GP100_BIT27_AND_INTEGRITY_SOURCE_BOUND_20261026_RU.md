> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# GP100 IFR speed/bit 27 и integrity: граница локальных источников

Только offline: локальные NVIDIA/Nouveau/Envytools source files и уже сохранённый GP100 warm-trace extraction. Никаких GPU/SSH/flash/MMIO операций. Reproducer: `python3 audit_ifr_speed_semantics_sources_20261026.py` → PASS; полный pinned result — `IFR_SPEED_SOURCE_AUDIT_20261026.json`.

## Что подтверждает первичный источник

- Локальный Nouveau `rnndb/bus/pci.xml` (Envytools `f102b82381f3f11cee113d16374c87091db039d9`, файл SHA в JSON) задаёт PPCI_2 base `0x8c000` для `GK104-`; `CONFIG_LINK` расположен в `+0x40` (`0x8c040`). Для CONFIG_LINK он именует directive `[3:0]`, где `1=CHANGE_SPEED`, status bit4 и speed `[19:18]`; для поколения `G80:TU102` значения speed `0/1/2` помечены `8/5/2.5 GT/s`. В этом entry **нет определения bit27 или всего поля `[31:27]`**. Это поколенчески широкая register map с диапазоном `GK104-`, не явная GP100 bit27 declaration.
- NVIDIA published GP100 headers в закреплённом checkout содержат `dev_bus_addendum.h` для SW_SCRATCH30.GPU_RESET_REQUIRED; `dev_bus.h` и `dev_nv_xp.h` в `pascal/gp100` отсутствуют. Поэтому локальная база не задаёт официальное имя/значение `0x8c040[27]`, `0x8872c` или `0x8c2c0 CYA_0/DIS_G2`. Соседний GP102 header описывает `CYA_1` по `0x8c300`, bit4 `BLOCK_HOST2XP_HOLD_LTSSM`; это другой регистр/поколение и не определяет CYA_0.
- Nouveau `gk104.c` реализует speed writer маской `0xc0000` на `0x8c040` и затем pulse bit0. Однако GP100 constructor в `gp100.c` содержит только PCI config aperture и MSI rearm; он не связывает таблицу с GK104 PCIe callbacks. Это подтверждает существование GK104 implementation, но не применение её к GP100 runtime.

## Что говорят IFR/PBUS источники — и чего не говорят

- NVIDIA TU102 `dev_bus.h` определяет `NV_PBUS_IFR_FMT_FIXED0/1/2`, NVGI signature, software version, fixed-data size и total-data-size. В этом header нет `FIXED*_CHECKSUM` или `FIXED*_PARITY`. NVIDIA TU102 VBIOS routine `s_romImgFindPciHeader_TU102` читает эти locators и использует version/size для поиска flash-status/ROM-directory/image offset; в проверенном полном bounded body он не проверяет IFR checksum или parity. Это Turing ROM-directory locator, не GP100 IFF interpreter.
- GV100 `dev_bus.ref.txt` называет `IFR_ERROR.BADPARITY` bit8, `BADCHECKSUM` bit16 и `IFF_BADCMDOP` bit27 в PBUS IFR error status по `0x1728`. Последний — **ошибка формата/команды в другом регистре**, не семантика BAR0 `0x8c040[27]`, и эти status labels не задают checksum equation/range для FIXED1/2.
- Nouveau `shadowrom.c` и Turing RM routine — поздние ROM locator/parser paths. Они не доказывают hardware IFR acceptance. Закрытый локальный `IFR_PRIMITIVE_VERDICT_RU.md` уже помещает type-`0x82` interpreter до извлечённого Falcon payload; этот проход не повторял его parser audit.

## GP100 cross-check по новым SPI и сохранённому trace

Обе новые CMP SPI-копии имеют SHA `8ae75ee4d1fc28e7c5e42982d16f726f40017b731ac046474e5207e82bb406ba`; P100 SHA `e687e66ca234663acfa94b013b5ed7169d261c81e096d9feb5bc1461457cde34`. На физическом file offset `0x1ec`:

| Вход | Bytes `0x1ec..0x1f7` | Следствие только из operands |
|---|---|---|
| CMP03 | `42 c0 08 82 ff ff ff 07 00 00 00 08` | target `0x8c040`, mask `0x07ffffff`, value `0x08000000` |
| P100 | `42 c0 08 82 ff ff ff ff 00 00 00 00` | тот же target, identity pair |

**Исправление по первичному binary audit:** этот `0x1ec` кандидат нельзя называть speed-only и нельзя включать в Gen2x1 edit. Закреплённый audit GEN23_BIT27_WIDTH_CORRECTION_20261022.txt (archive reference: `../../evidence_20261022_photo_followup/GEN23_BIT27_WIDTH_CORRECTION_20261022.txt`; original context may be excluded), SHA-256 `352c5e42bbe85c5328f3c3c3f0a3a7e678991bd177c301d202a2b7d3e4893f24`, сопоставил две независимые MODS реализации: MODS367 ELF `loaded_firmware_20261001/tool_inputs/cmp_analysis_mods367.unpacked` (SHA `74daa3f6237d7fe88f4be102ae1f638b8d95e91d731f0eda09d1a991f9f820cb`), VMA `0x150ff38..0x150ff6f`, и MODS400 400.250 ELF `evidence_20261022_photo_followup/mods400_all_builds_unpacked/400.250/mods.unpacked` (SHA `7f4494964a8a0086ebb0574b04f515f7e77d1359c651d4f897a353fa765dfb94`), VMA `0x1cbaef0..0x1cbaf25`. Оба конструктора читают ширину `BAR0+0x8c080 & 0x1f`, сдвигают на 27 и объединяют с `BAR0+0x8c040 & 0x07ffffff`. Закреплённый GP100 HAL evidence JSON (SHA `19eb82119d9ab6afae02ca5ffceb082ef2dac21b581de18ed0aac24a5560758d`) устанавливает связь с GP100 BIF/HAL семейством, но не доказывает, что PCI ID `15c2` вызывает именно этот конструктор.

Следовательно, no-op/identity edit этой IFR записи удаляет width-setting запись, а не доказанный Gen1 speed clamp. `0x1ec` record **исключён из Gen2x1**. Для Gen2 исследовать отдельно `[19:18]` и speed-control consumer, сохраняя неизменным всё `[31:27]`. Прежняя speed-only модель superseded; trace XOR сам по себе не является истолкованием поля.

Сохранённый trace audit JSON SHA `6f6e4502005e87499f1675019f626d6b98e3f0d625118365ca6cd72645f0b828`, underlying trace SHA `3de29507b1e9baf3c40429a2dfa363d1452d35d362a682ee33445f3563c78461`: CMP03 `0x8c040=0x08089000` (R line 2343135, W line 2343137), P100 `0x80009000` (R 1324535, W 1324537). XOR `0x88080000` затрагивает биты `31,27,19`; по Envy map bits `[19:18]` относятся к speed, а bit27/top bit31 в этой записи остаются без GP100 имени. CMP03/P100 на этом boot имели разные `LnkCap`/`LnkSta`, что делает width interpretation правдоподобной, но корреляция не закрывает field consumer.

На `0x8c2c0` сохранены CMP03 `0x068711b3` и P100 `0x060711b2`, XOR `0x00800001` (только bits23 и0; **bit2 равен нулю у обеих**). Envytools явно описывает `PCIE_V2_SUPPORTED` bit2 по **соседнему адресу `0x8c1c0`**, это не определение `0x8c2c0`. Локальный CMP170HX community проект описывает GA100 `CYA_0[2]=DIS_G2`, но такой перенос на GP100 не подтверждён. У `0x8872c` trace хранит только R=0 на трёх картах; первичного GP100 field/consumer определения не найдено.

GV100 community edit byte `flash 0x214: 0x42→0x02` из `spi_write_ifr_l3.py` ретаргетирует конкретную запись `0x8c040→0x8c000` (по авторской GV100 интерпретации — с CONFIG_LINK на read-only LINK_PRESENT). Это не измерение GP100, не edit CMP03 record по offset `0x1ec`, и не контроль GP100 `FIXED1` checksum/parity. Совпадение байта `0x42` само по себе не переносит семантику между разными offsets/records.

Trace — тёплый стандартный driver reload, `PC=0`, один terminal `Lost 60 events`; он фиксирует сохранённые значения/доступы, но не caller attribution, холодный POST или полный IFF execution.

## Ровно недостающий источник

Для полной dispatch attribution не хватает точного PCI ID `15c2` HAL selection/callsite, который достигает MODS367 width constructor; существующий selector audit закреплён по SHA `3d3347c9fdb4098b1294e47062941ee7a7f4c4e668283d86c6ec8e358fbc79c9`. Уже имеющийся constructor/HAL evidence достаточен, чтобы исключить IFR `0x1ec` из Gen2x1. Для `0x8c2c0`/`0x8872c` по-прежнему нужен GP100-owned register header/manual/callsite, а не CYA0 label от GA100 или `0x8c1c0` field. Для IFR integrity остаётся нужен GP100 IFF consumer/contract с точным algorithm и covered byte range; `sum8`/parity — неподтверждённые модели по stock corpus.
