> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# CMP02: анализ двух новых NVFlash full-image reads

## Краткий результат

Два сохранения `NVFlash 5.692 --index=1 --save … --entire` дали одинаковый файл размером 524288 байт, SHA-256 `74f2eba8dcf8d1ed14922a34493ddafa013830bcc1635af6c47950fe75a6e878`. В обоих файлах NVGI начинается с `NVGI`; в единственной полной OBD v1.1 записи по file offset `0x38190` serial field `0x381b4..0x381c3` равен `BOARD-SERIAL-REDACTED`. Это связывает сохранённые байты с целевым CMP02 по аппаратному serial. Run log связывает тот же serial с UUID `GPU-REDACTED`, BDF `0000:02:00.0`; NVFlash list перед save показал index `<1>` на bus 02, device 15c2/subsystem 12b4.

В обоих новых образах IFR заканчивается на `0x840`, сумма байтов до конца IFR mod 256 равна `0x00`. Запись типа `0x82` для `0x8c040` находится по file offset `0x1ec` и содержит `42c00882ffffff0700000008`: операнды `0x07ffffff` и `0x08000000`. При ранее рассматривавшейся арифметике `(old & mask) | OR` high-five поля `[31:27]` получается `1`, если исходное значение считать нулём. Это **арифметика операндов**, не доказательство GP100 формулы type `0x82`, фактически применённой ширины либо того, что запись успела сформировать PCIe config-space capability до ответа хоста.

Соседняя запись `0x88084` по offset `0xac` равна `868008820ffcffff10000000` (операнды `0xfffffc0f / 0x10`). Она байтово совпадает с двумя сохранёнными CMP factory SPI-образами. `0x88084` и `0x8c040` — разные NVGI destinations; значение первой записи не является чтением второй или доказательством ширины x16.

## Сравнение с архивными образами

| Вход | OBD serial | `0x88084 @ 0xac` | `0x8c040 @ 0x1ec` | High-five arithmetic при old=0 |
|---|---|---|---|---:|
| Новый CMP02 NVFlash read 1/2, SHA `74f2eba8…` | `BOARD-SERIAL-REDACTED` | `868008820ffcffff10000000` | `42c00882ffffff0700000008` | 1 |
| CMP03, CH341 intake, SHA `8ae75ee4…` | `BOARD-SERIAL-REDACTED` | то же | то же | 1 |
| CMP01, CH341 intake, SHA `3e25541f…` | `BOARD-SERIAL-REDACTED` | то же | то же | 1 |
| P100, CH341 intake, SHA `e687e66c…` | `BOARD-SERIAL-REDACTED` | `86800882fff3ffff00000000` | `42c00882ffffffff00000000` | 0 (identity operand) |
| V6 MATCH candidate, SHA `b5df0ed7…` | `BOARD-SERIAL-REDACTED` | как P100 | как P100 | 0 (identity operand) |
| V6 TRIPLET candidate, SHA `9b8c3925…` | `BOARD-SERIAL-REDACTED` | как P100 | как P100 | 0 (identity operand) |

У новых CMP02 bytes exact equality с двумя архивными CMP образами установлена для обоих указанных IFR records, но эти CH341 образы принадлежат serial других плат. V6 serial также другой. Совпадение этих записей не переносит идентичность или factory provenance на CMP02. Полные same-offset сравнения и точные runs для исторических источников содержатся в JSON; например, новый файл отличается от CMP03 образа на 9 байт и от CMP01 на 8 байт, преимущественно в OBD-tail, но не идентичен им. Сравнение не является проверкой полного firmware trust chain.

Существующий bounded IFR audit указывает, что type-`0x82` обработка происходит до извлечённого Falcon DevInit payload, но не устанавливает точный GP100 formula/integrity contract и не доказывает момент относительно формирования PCIe `LnkCap`. Смотрите `../ifr_path/IFR_MAXLANE_PRODUCER_RU.md`, `../../../../IFR_PRIMITIVE_VERDICT_RU.md`, `../../../../V6_LINEAGE_RESULT_RU.md` и `../../../../evidence_20261022_photo_followup/GP100_IFR_TYPE82_EXECUTION_BOUNDARY_20261024_RU.md`. Старый вывод о «V6 x16 outcome» не подтверждён первичным SPI write/readback вместе со связанным x16 лив-логом.

## Источник, целостность и пределы

- Входы: `../inputs/cmp02-full-spi-read-1.bin` и `../inputs/cmp02-full-spi-read-2.bin`; оба 512 KiB, оба hash выше и полностью равны.
- Первичный acquisition trace: `../runtime.tar` (SHA-256 `92b8f9d9809b935971cfee251d4cd5fb82c5ae48d315b242a65bc66ada1a43c6`), содержащий runner, JSONL события, summary и два raw outputs. `../run.stdout` (SHA `a02a648a00655530b991cd89e12891be65d3d05f4bd655c79fe33c6c59acabc6`) показывает обе успешные save-команды, EEPROM ID `EF,6013 WBond W25Q40EW 1.65-1.95V`, completion и mapping adapter index 1→bus 02.
- Это два повторных **software reads через NVFlash**, не независимые reads программатором/клипсой и не доказательство, что содержимое не менялось ранее. Приём `--entire` и размер файла сами по себе не доказывают покрытие физического чипа без пропусков.
- В этом же trace восстановление после снятия драйвера **не завершилось успешно**: `cmp100-12gb.service` restart упал по timeout; candidate правильно withheld derivative modules и monitor; финальная проверка модулей завершилась ошибкой. `pci_after_profile_timeout.json` подтверждает BDF/driver присутствие и `LnkSta`, но не подтверждает профиль 12 GiB или полное здоровье GPU. Этот анализ не заявляет, что host/card сейчас восстановлены или здоровы.
- Входные ROM/SPI байты не изменялись; никаких новых image output файлов не создавалось.

## Воспроизведение

Из корня workspace:

```sh
python3 evidence_20261029_cmp02_x16/max_lane_producer/current_target_spi_read/analysis/audit_current_cmp02_spi.py
```

Скрипт проверяет закреплённые SHA/размеры, обходит NVGI stream до declared IFR end, достаёт точные записи `0x88084`/`0x8c040`, ищет полные OBD records, затем делает побайтовые сравнения с двумя CH341 CMP образами, P100 и двумя V6 файлами. Результат в `CURRENT_CMP02_SPI_ANALYSIS.json`; скрипт, JSON и данный отчёт — офлайн-анализ, не hardware validation.
