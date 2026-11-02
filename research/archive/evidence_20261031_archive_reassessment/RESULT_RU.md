> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Пересмотр архивов CMP100: что удалось дополнить

Работа выполнена офлайн. Это отдельное офлайн-дополнение к сохранённому состоянию 31 октября. Сервер, MODS, GPU, reset, драйверы и SPI не запускались/не менялись. Цель x16 остаётся на паузе; утверждения ниже не являются результатом нового аппаратного опыта.

## Самая полезная новая зацепка по x16

Найден **один конкретный путь добавления в настоящий reset-list** MODS520.112: `0xebf500..0xec00c9` → call `0xebf9ee` → append-helper `0xe125f0`, с контейнером из global `0xf7a7fb8`. Запись имеет два qword, 16 байт; изученный путь создаёт `NvLinkDevIf::SimGpuDev` и reference-counted control block. Контейнер определяется через RTTI как `TestDeviceMgr`.

Это дополнение к прежнему handoff, где producer списка оставался неизвестным. Закрыт **конкретный статический SimGpuDev-путь**, а не полный вопрос о producer физических GP100. Второй найденный вызов того же helper `0xebfee4` не засчитан: RBX меняется до него, и связь с reset global не доказана. Косвенные/inlined producers, источник входного вектора и действие `-only_pci_dev` остаются открытыми.

Проверены исходный SHA pinned ELF, точные инструкции, адреса и `.eh_frame` границы. Родитель независимо повторил replay: PASS. Обнаруженные при review неточные границы функции, offsets RTTI и чрезмерная атрибуция второго call исправлены до итогового вывода.

- [Полная трасса](pcie_trace/RESET_PRODUCER_TRACE_RU.md)
- Replay (archive reference: `pcie_trace/reset_producer_replay.py`; original context may be excluded)
- Независимый parent PASS (archive reference: `parent/RESET_PRODUCER_PARENT_REPLAY.json`; original context may be excluded)
- FDE границы (archive reference: `parent/FDE_BOUNDS.txt`; original context may be excluded)

## Привязка SPI к платам: закрыта архивная неоднозначность

Восстановлена сквозная связь OBD serial → сохранённые OEM GET payload/stdout → UUID/BDF, отдельно от более позднего same-run NVFlash acquisition.

| Набор | Представленный serial | Привязка в сохранённых receipts |
|---|---|---|
| CH341 `Cmp100-100_2*`, 26 октября | `BOARD-SERIAL-REDACTED` | CMP03, UUID `GPU-ID-REDACTED…` |
| CH341 `Cmp100-100_3*`, 26 октября | `BOARD-SERIAL-REDACTED` | CMP01, UUID `GPU-ID-REDACTED…` |
| CH341 `p100_*`, 26 октября | `BOARD-SERIAL-REDACTED` | P100, UUID `GPU-ID-REDACTED…` |
| Два software NVFlash read, 29 октября | `BOARD-SERIAL-REDACTED` | Нынешняя CMP02, UUID `GPU-ID-REDACTED…`, BDF `02:00.0` |

Среди CH341 входов 26 октября **нет образа с serial нынешней цели**. Её сохранённый образ связан с её runtime identity через тот же run, но методом остаётся software NVFlash read. Это metadata identity join, не доказательство независимого чтения физического SPI, неизменённых заводских байтов или физических HBM-параметров. Не переносить выводы по старым платам на текущую только по названию файла.

- [Сверка и ограничения](hbm_trace/CMP_SPI_RUNTIME_IDENTITY_JOIN_RU.md)
- Checker (archive reference: `hbm_trace/audit_cmp_spi_identity_join.py`; original context may be excluded) и parent replay (archive reference: `parent/SPI_IDENTITY_PARENT_REPLAY.stdout`; original context may be excluded)

## Найдено ранее пропущенное HBM-вложение

В предыдущей ссылке ошибочно записан сегмент UUID `6be6` вместо `be6c`. Файл существует, прочитан и сохранён здесь точной копией: HBM attachment (archive reference: `inputs/HBM_MODEL_ATTACHMENT_8ba0cd1d.txt`; original context may be excluded), SHA `4e9301980c00fb6522c1ca5eb5e01c93510399ccab577d45586b7d082243ec65`. Происхождение (archive reference: `inputs/RECOVERED_ATTACHMENT_PROVENANCE.json`; original context may be excluded).

В нём предлагаются SPD/I2C, подсчёт термодатчиков, PCIe EQ как измеритель HBM и MR8/MRR. Проверяемого GP100-интерфейса или первичных измерений для этих предложений не приведено. Команды из вложения не запускались и не перенесены в аппаратный план. Другие локальные pasted attachments также классифицированы; частичное чтение смешанных/длинных текстов отмечено честно: triage (archive reference: `crossref_notes/ATTACHMENT_TRIAGE_RU.md`; original context may be excluded).

Выявлена конкретная ошибка размерностей: строка 22 называет восемь memory dies «4-Hi». В предположении бинарных gigabit ёмкость одного стека равна `число memory dies × плотность die / 8`:

| Memory dies / высота | Плотность die | Ёмкость стека |
|---|---:|---:|
| 4 / 4-Hi | 4 Gib | 2 GiB |
| 4 / 4-Hi | 8 Gib | 4 GiB |
| 8 / 8-Hi | 4 Gib | 4 GiB |
| 8 / 8-Hi | 8 Gib | 8 GiB |

Следовательно, объём 4 GiB сам по себе не выбирает между 4-Hi и 8-Hi. Это математическая неоднозначность, не измерение нашей платы. [Проверка](parent/HBM_HEIGHT_DENSITY_CONSISTENCY.json).

Официальный [NVIDIA GP100 whitepaper, печатная стр. 17](https://images.nvidia.com/content/pdf/tesla/whitepaper/pascal-architecture-whitepaper.pdf#page=17) описывает общие варианты HBM2 и начальную конфигурацию P100 с четырьмя четырёхкристальными стеками. Это не устанавливает ни поддержку GP100 конкретных 8-GiB пакетов, ни их наличие на CMP. Поэтому уверенный запрет 8-Hi из найденной вставки не обоснован приведёнными ею источниками; противоположное утверждение о совместимости тоже не доказано.

## Что ещё проверено по прежним задачам

Четыре существующих реальных `cpu_snapshot_*.json` bridge-снимка сохраняют global `GpuDevMgr` `0xf7a7fd0`, а не восемь байт reset-list slot `0xf7a7fb8`. Проверены именно интервалы сохранённых сырых данных: присутствие адреса внутри общей memory mapping не равно сохранению его содержимого. Эти четыре снимка не заменяют запланированное early-mode0 наблюдение. Bounded audit (archive reference: `parent/ARCHIVED_CPU_BRIDGE_REUSE_AUDIT.json`; original context may be excluded).

Вход `0xfa85850`, используемый virtual predicate producer, по ELF находится в zero-filled RW области; это не читаемый из файла готовый descriptor. Его назначение остаётся неизвестным. PT_LOAD mapping (archive reference: `parent/ELF_ADDRESS_BACKING.json`; original context may be excluded). Поиск двух конкретных форм direct rel32 ссылок на `0xebf500` в executable sections дал ноль кандидатов; это не исключает virtual/indirect dispatch. Bounded locator (archive reference: `parent/RESET_SIM_PRODUCER_DIRECT_CALLERS.json`; original context may be excluded).

## Дополнение после проверки качества

Каталог всех исследований лежит в `ARCHIVE_PREP_20261031`. Этот каталог в тот mapping не входит. При сверке исправлен один неверный полубайт SHA pinned ELF в `pcie_trace/RESULT_RU.md` (`f8f74d` → фактический `f8e74d`). Пять дополнительных снимков `cpu_snapshot` от 30 октября тоже не содержат байт reset-global.

Владелец `0xebf500` найден: это vtable-слот `+0x20` класса `NvLinkDevIf::TopologyManagerProtobuf`, фабрика регистрируется из `.init_array[698]` под именем `TopologyManagerProtobuf`. Это NVLink topology manager, не показанный путь физического GP100. Входной vector и вызов слота на живом объекте остаются открытыми. Запись: [pcie_trace/TOPOLOGY_MANAGER_REGISTRATION_RU.md](pcie_trace/TOPOLOGY_MANAGER_REGISTRATION_RU.md). Сводка проверки: [QUALITY_RECHECK_20261031_RU.md](QUALITY_RECHECK_20261031_RU.md).

На pinned MODS 520.112 высота стека в разборе Device ID — это бит 7, поле `record+0x11`: 0 печатается как 4, 1 как 8. Путь включается байтом `объект+0x90`, который ставится при `+0x2c == 2` у настройки `HbmDeviceInitMethod`. Обмен идёт через `WriteHost2Jtag` и смещения `0xc800/0xc808/0xc804`; при отказе JTAG функция возвращает `0x114` до хвостового обнуления `0xc800`. Живого ID и признака четвёртого стека это не даёт. Запись: [hbm_trace/MODS520_PASCALHBM_DEVICE_ID_STACK_RU.md](hbm_trace/MODS520_PASCALHBM_DEVICE_ID_STACK_RU.md).

## Приоритет следующей работы

1. **Офлайн x16:** владелец и статическая регистрация `0xebf500` установлены, см. дополнение выше. Остаётся происхождение входного вектора и вызов слота `+0x20` на объекте с vptr `0x4c692c8`. Только после этого можно судить, покрывает ли `-only_pci_dev` reset-list. Не подменять эту коллекцию `GpuDevMgr`.
2. **HBM:** программное поле высоты разобрано на 520.112. Дальше нужен первичный идентификатор пакета или уже снятый физический ID. Повторять чтение Device ID или менять `HbmDeviceInitMethod` из этого разбора нельзя: режим на платах неизвестен, а ветка `0x114` не обнуляет `0xc800`.
3. **Аппаратный этап:** normal-MODS mode0 observer остаётся невыполненным и требует отдельного возобновления по актуальному handoff. Старые MAX/SetWidth/RL/LD операции без новой предпосылки повторять не предлагается.

Нового подтверждённого способа получить x16, включить четвёртый HBM site или установить 8 GiB/stack в этих материалах нет. Выполнены конкретные офлайн-подзадачи: восстановление источника, сверка происхождения образов, нахождение одного producer path и проверка повторного использования старых captures.

## Сохранность подготовленного переноса

Новые результаты находятся только в `evidence_20261031_archive_reassessment/`. Этот каталог и восстановленное вложение ещё не входят в прежний frozen transfer mapping; при будущем переносе их нужно включить отдельным дополнением или новой версией manifest. Старый `ARCHIVE_PREP_20261031` не переписывался. Фактический перенос не выполнялся.
