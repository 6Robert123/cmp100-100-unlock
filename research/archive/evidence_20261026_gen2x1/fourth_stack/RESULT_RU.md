> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# 4-й стек: CTRL_FBIO (0x21938) override ПРИНИМАЕТСЯ — зелёный на MODS floorsweep

Дата: 2026-10-27. Площадка: SECONDARY-HOST (SECONDARY-HOST, PRIVATE-IP), чистая CMP100-100
`0000:01:00.0` (GPU-ID-REDACTED), 6144 МиБ, Gen1 x1. Драйвер 580.178.04, ядро 7.0.0-38.
Опыт (с подтверждением, reversible self-restoring): запись 0 в CTRL_FBIO через
resource0 mmap, read-back, возврат оригинала. Не ретренит память.

## [ИЗМЕРЕНО] (ctrl_fbio_probe_userspace.py)
```
PRE   0000:01:00.0 ctrl_fbio=0x00000003 opt_fbio=0x0000000f opt_fbp=0x00000003
WRITE ctrl_fbio 0x00000003 -> 0 readback=0x00000000 took=1   <- override ПРИНЯТ
RESTORE ctrl_fbio=0x00000003 restored_ok=1
```
Health: карта цела (6144 МиБ, 0% util, 0 новых Xid/AER), значение восстановлено.

## [ВЫВОД]
CTRL_FBIO `0x21938` — **писабельный runtime-регистр**: запись 0 прилипает (readback=0),
в отличие от gen-фьюз-OPT `0x2157c`, который запись дропал. То есть floorsweep-override
достижим на config-слое. Это ровно GO-условие из прежнего `gp100_ctrl_fbio_probe.c`:
«readback == test (0x00) -> override honoured; devinit-патч имеет реальный механизм
(но не гарантия, что 4-й стек тренируется)».

Важно (ограничение): live-запись `0x21938` НЕ ретренит память (тренировка на devinit),
поэтому одним poke 4-й стек не поднять. Значение took=1 — necessary, не sufficient:
нужно применить override ДО тренировки (MODS floorsweep / devinit-мод) и проверить,
тренируется ли физически 4-й интерфейс (фьюз OPT_FBIO=0x0f — источник; может ещё
hard-gate'ить FBPA ниже, это и проверит MODS).

## Следующий шаг
MODS floorsweep-override на SECONDARY-HOST: MODS сам инициализирует GPU с `fbio_enable:0xf`
ДО тренировки → покажет, поднимается ли 4-й стек (16 ГБ). Flash не трогаем, откат=reboot.
MODS на SECONDARY-HOST нет — требует установки/портирования. Это больший объём.
