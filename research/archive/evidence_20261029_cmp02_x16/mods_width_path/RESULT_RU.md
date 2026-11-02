> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

> **Вывод о width stubs отозван.** Callsite подтверждает destination=BIF+0x2c0. Реальные targets: getter 0x11af1f0, width callback 0x11af740, helper 0x11af320; это не прежние stubs. См. исправленный receiver-аудит (archive reference: `REPORT_RECEIVER_BASE_RU.md`; original context may be excluded). Нижние GPU register methods ещё не прослежены до hardware sink. Текст ниже сохранён как история ошибочного сопоставления offsets; не применять его как актуальный dispatch-вывод.

# MODS GP100 width callback: bounded static result

## Result

В MODS367 для зарегистрированной HAL-таблицы с именем **GP100**, ID `0x36`, не нашёлся рабочий callback смены PCIe ширины. Generic `bifLinkWidth_IMPL` вызывает table slot `+0x370` для width-read и `+0x498` для запроса изменения при несовпадении. Конструктор `0x112d090` копирует в BIF object первые `0xa7` qword из таблицы `0x2981580`, так что оба слота находятся в копируемом диапазоне:

- `+0x370 → 0x108cc00`; raw bytes `b8 56 00 00 00 c3` (`mov eax,0x56; ret`): getter возвращает `NV_ERR_NOT_SUPPORTED`.
- `+0x498 → 0x108ce50`; raw bytes `f3 c3` (`rep ret`): callback не содержит действия или перехода к коду записи.

Следовательно, generic width API не предоставляет через этот GP100 table рабочий путь изменения ширины. Отдельное тело с меткой `bifSetLinkWidth_GF100` есть в общем MODS367 бинарнике и содержит широкий callback/poll flow, но **оно не выбрано в slot `+0x498` этой GP100 таблицы**. Не следует переносить его наличие в бинарнике на HAL ID `0x36`.

В MODS400.250 generic wrapper всё ещё присутствует, но слоты уже другие (`+0x458` read и `+0x578` change); конкретный GP100 table mapping этой версии в этом узком аудите не установлен. Отдельный старый audit 400.184 указывает GP100 template slot `+0x70` на `bifSetPLLinkConfigMaxLinkRate_GP100`, который редактирует rate `[19:18]`; это не setter ширины, и 400.184 нельзя выдавать за 400.250.

## Reproducer и raw evidence

Запустить `python3 audit_width_path.py`: проверяет полные SHA входных ELF, точные байты constructor/call sites, qword значения slots и ветви stub; пишет `AUDIT_FACTS.json`. Raw disassembly сохранён отдельно:

- `mods367_bifLinkWidth_IMPL.disasm.txt` — generic wrapper, его calls через `+0x370/+0x498`.
- `mods367_gp100_hal_width_slots.disasm.txt` — цели GP100 table slots.
- `mods367_GF100_width_change_body.disasm.txt` — общий GF100-named body, только для контраста.
- `mods400250_bifLinkWidth_IMPL.disasm.txt` — generic MODS400.250 wrapper; HAL slot selection тут не доказана.

Input SHA-256: MODS367 `74daa3f6237d7fe88f4be102ae1f638b8d95e91d731f0eda09d1a991f9f820cb`; MODS400.250 `7f4494964a8a0086ebb0574b04f515f7e77d1359c651d4f897a353fa765dfb94`. Источник table→HAL registration — `mods_gp100_hal_evidence.json` (archive reference: `../../loaded_firmware_20261001/mods_gp100_hal_evidence.json`; original context may be excluded), чей audit привязывает ID `0x36` к строке `GP100` и table constructor. Восстановление не привязывает PCI ID `10de:15c2` к HAL ID `0x36`; это существенное ограничение.

## Вывод для x16

В пределах проверенного MODS GP100 HAL callback route нет оснований пытаться задействовать `0x8c040[31:27]` как x16 setter; этот маршрут до него не доходит. Это не доказывает отсутствия иного механизма в любом firmware/HAL. Полную последовательность PHY quiesce/reset/training для GP100 width change не обнаружил: найденный generic GF100 body не является доказанным исполняемым callback данного GP100 table. Аудит статический; MODS и GPU не запускались, железо не менялось.
