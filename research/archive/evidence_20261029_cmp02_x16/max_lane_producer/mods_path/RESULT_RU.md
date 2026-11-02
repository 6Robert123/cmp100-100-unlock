> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# MODS367 `8c040[31:27]`: source этого поля и границы вывода

## Результат

В pinned MODS367 найдено точное тело, содержащее ветку `0x150ff38`: DWARF FDE `0x150fca0..0x15100ee`, вход SHA-256 `74daa3f6237d7fe88f4be102ae1f638b8d95e91d731f0eda09d1a991f9f820cb`. Полный FDE сохранён в `high5_source_full_fde.disasm.txt` (archive reference: `high5_source_full_fde.disasm.txt`; original context may be excluded); replay `replay_high5_source.py` (archive reference: `replay_high5_source.py`; original context may be excluded) проверяет FDE по `objdump`, raw input hash и единственную data pointer reference на функцию. Результат `HIGH5_SOURCE_AUDIT.json` (archive reference: `HIGH5_SOURCE_AUDIT.json`; original context may be excluded): `PASS_BOUNDED_STATIC`.

В ветке, достигнутой по sentinel `0xdeadcAD1`, последовательность инструкций такова:

1. Через метод `[RBX+0x3c0]` читается `0x8c080`; результат маскируется `0x1f`.
2. Тем же dispatch slot читается `0x8c040`; сохраняются только `[26:0]` (`AND 0x07ffffff`).
3. Прочитанное из `8c080[4:0]` значение сдвигается на27 и объединяется с `8c040[26:0]`.
4. В обычной ветке из результата убираются младшие четыре бита, после чего DWORD помещается в выходную память по `[R15]`; следующий элемент record-list имеет stride `0x0c`.

В этой ветке FDE нет BAR store, прямой записи `8c040`, PHY reset, retrain или training poll. Поэтому найденное тело **собирает/нормализует значение по уже прочитанному `8c080`**, а не устанавливает width target через доказанную hardware write. Это сильнее согласуется с mirror доступного/current width, чем с самостоятельной lane programming. Но момент относительно фактической PCIe training не доказан: caller/phase путь для этой функции остаётся неразрешённым.

## Caller, таблица и HAL registration

Внутренние error strings называют функцию `pgislandPatchPrivRestoreState_GP102` и source path `../../../../resman/kernel/lpwr/pascal/pgislandgp102.c`: это GP102 PGISLAND private-restore patcher, а не установленный общий GP100 PHY setter.

HAL name table и registration stubs дают соседние ID: `0x36` / `GP100` (`0x30030b0/0x30030b8`) и `0x37` / `GP102` (`0x30030c0/0x30030c8`). Stubs `0x112dbd0` и `0x112dbe0` передают соответствующие ID и aggregate tables `0x29d1ac0` / `0x29d1800`.

Для GP102 aggregate slot `+0x1b0` указывает на constructor `0x110bab0`; он копирует `0x30` qwords с `0x2980ec0`. Qword index `0x1a`, VMA `0x2980f90`, равен функции `0x150fca0`. Значит GP102-named PGISLAND table включена в GP102 aggregate.

У GP100 aggregate соответствующий slot `+0x1b0` указывает на другой constructor `0x110ba90`, который копирует `0x30` qwords с `0x29882c0`; этот диапазон не включает `0x2980f90`. FDE-ограниченный просмотр constructor functions в GP100 aggregate qword slots `+0x0..+0x278` не нашёл прямого copy edge на table `0x2980ec0`. В этом bounded registration path GP100 не использует найденную GP102 PGISLAND table. Это не исключает любую иную динамическую/shared-code связь во всём ELF; выбранный runtime HAL для конкретного PCI ID `10de:15c2` также не доказан.

FDE вызывает read-like dispatch `[RBX+0x3c0]` с offset в `ECX`. Этот receiver отличается от GP100 BIF receiver из `REPORT_RECEIVER_BASE_RU.md` (archive reference: `../../mods_width_path/REPORT_RECEIVER_BASE_RU.md`; original context may be excluded); нельзя переносить сюда его delta `0x2c0`. Установленный table/constructor выше относится к GP102 PGISLAND path, не к GP100 BIF table `0x2981580`.

## Другие high5 writers в ограниченных первичных путях

- Для CMP IFR record `0x1ec` operands `mask=0x07ffffff, OR=0x08000000` по прежней arithmetic model принудили бы high5 к1 при сохранении low27. Это не доказанная BAR write: фактический type-`0x82` consumer, формула применения и timing не установлены (bounded IFR audit (archive reference: `../ifr_path/IFR_MAXLANE_PRODUCER_RU.md`; original context may be excluded)).
- GP100 MODS width callback `0x11af320` формирует candidate через GPU `[+0x388]` и читает результат через `[+0x3c0]`; сохранённый width-path audit отслеживает изменения candidate в `[22:20]`, а не показывает high5 setter. Его physical write sink остаётся отдельной границей доказательства.
- В рассматриваемом FDE high5 формируется только из чтения `8c080[4:0]` и записывается в output buffer. В просмотренных этих bounded sources другой доказанный runtime high5 writer не найден. Это не заявление об отсутствии иных writers во всём ELF или firmware corpus.

## Replay

`python3 evidence_20261029_cmp02_x16/max_lane_producer/mods_path/replay_high5_source.py` → `PASS_BOUNDED_STATIC`. Replay проверяет FDE, HAL enum IDs/stubs, GP100 и GP102 aggregate slots, FDE-limited copy ranges и сохранённые disassembly excerpts. Он только читает закреплённый ELF и запускает локальный `objdump`; hardware, SSH, build, writer и новая PCIe проба не использовались.
