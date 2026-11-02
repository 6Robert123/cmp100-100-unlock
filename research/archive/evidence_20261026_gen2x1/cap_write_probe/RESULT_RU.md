> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Запись 0x8c1c0 не прилипает; корень Gen2-лока — рекламируемая PCIe-capability

Дата: 2026-10-26. boot_id `UUID-REDACTED`. Цель CMP03
`GPU-ID-REDACTED…`, BDF `0000:03:00.0`, BAR0 `0xf1000000`.

## Опыт 1 (ЗАПИСЬ, с подтверждением оператора): self-restoring poke 0x8c1c0

Модуль `cap_write_8c1c0_selfrestore.c` (lab SHA
`c06bc7f472d183b80809a4837aa02a2b106838c203e4ca77e2fd0955bfc5cf32`): assert
`pre==0x00210036`, затем `writel(0x00010036)` → readback → немедленный
`writel(0x00210036)` (restore) → readback; `-EAGAIN`, не резидентный.

[ИЗМЕРЕНО] dmesg (`write_probe.txt`):
```
CAPW PRE          off=0x8c1c0 val=0x00210036
CAPW AFTER_WRITE  wrote=0x00010036 readback=0x00210036   <- запись НЕ применилась
CAPW AFTER_RESTORE wrote=0x00210036 readback=0x00210036 restored_ok=1
```
Health: до/после UUID/BDF те же, линк CMP03 1x1, **0 новых Xid/AER**, модуль не
резидентный, `0x8c1c0` после = `0x00210036`.

[ВЫВОД] Прямой BAR0-`writel` в `0x8c1c0` игнорируется (readback = оригинал, без
poison). `0x8c1c0` — зеркало/HwInit, не защёлка под простой MMIO-poke.

## Опыт 2 (READ-ONLY): корень — PCIe config LnkCap/LnkCap2

[ИЗМЕРЕНО] `setpci`/`lspci -vv` (config space, только чтение):

| поле | CMP03 | P100 |
|---|---|---|
| LnkCap (CAP_EXP+0x0c) | `0x00453c11` | `0x00454103` |
| &nbsp;&nbsp;Max Link Speed `[3:0]` | `1` (2.5GT/s, Gen1) | `3` (8GT/s, Gen3) |
| &nbsp;&nbsp;Max Link Width `[9:4]` | `1` (x1) | `0x10` (x16) |
| **LnkCap2 (CAP_EXP+0x2c) Supported Speeds** | **`0x02`** (только 2.5GT/s) | `0x0e` (2.5/5/8) |
| LnkCtl2 Target Speed (CAP_EXP+0x30) | `0` | `3` (Gen3) |

Связь с BAR0: config `LnkCap` = `0x00453c11` **точно равно** прочитанному ранее
BAR0 `0x88084` на CMP03 → блок `0x88xxx` зеркалит PCIe config space.

[ВЫВОД] CMP03 **аппаратно рекламирует Gen1-only**: LnkCap Max Speed=1 и LnkCap2
supported-vector=`0x02` (поднят только бит 2.5GT/s). Всё остальное —
`0x8c1c0[23:20]=2`, зануление `+954/955` в `_nv017281rm`, AND пяти
capability-источников — производные от этой рекламируемой способности. Значение
прошито при boot (VBIOS/strap/fuse), HwInit/read-only в рантайме, поэтому host-poke
мирроров (опыт 1) не применяется.

## Куда это сводит путь к Gen2 (рычаги)

Рантайм-host-запись исключена измерением (опыт 1). Остаются два пути, оба меняют
рекламируемую способность до/в обход HwInit-лока:
1. **ROM/IFR (SPI)**: изменить в прошивке PCIe-дефолты/IFR так, чтобы карта при boot
   поднимала LnkCap2 bit 5GT/s (Gen2). Ветка `SPI_PROM_LAYOUT…`, `IFR_ROM_SESSION…`.
   Осторожно: прежняя IFR-поправка показала, что `0x1ec`=width, не speed; нужно
   отдельно найти speed-поле. На cmp170hx (другая карта) SPI-запись была GFW-locked —
   для CMP100 статус записи SPI проверить отдельно.
2. **ACR/falcon secure-init окно**: привилегированная запись XVE-override способности
   до HwInit-лока — подход community GV100 (custom ACR). Ветка `gp100_sequence/acr…`:
   GP100 идёт по gm200/v1, доказанного payload пока нет.

## Альтернативы / что ещё не проверено

- LnkCap2 config-space запись (`setpci CAP_EXP+0x2c`) ещё не пробовалась; по PCIe
  LnkCap2 — HwInit (RO), ожидается что не применится, как мирор. Это ЗАПИСЬ →
  требует отдельного подтверждения; дёшево как различитель «принимает ли config-override».
- Возможен привилегированный backing-регистр supported-speeds, писать который можно
  только из CYA-unlocked/secure контекста (отсюда и нужда GV100 в ACR). Не локализован.
- strap/fuse-источник LnkCap2 на GP100 не прочитан (кандидаты — fuse-блок);
  read-only чтение возможно следующим шагом.

Gen2 x1 не достигнут. Блокер локализован на уровне PCIe config (LnkCap2=`0x02`).
