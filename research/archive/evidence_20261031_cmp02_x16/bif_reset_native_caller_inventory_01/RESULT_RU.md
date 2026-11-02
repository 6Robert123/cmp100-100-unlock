> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Поиск прямого native caller для `NV0080_CTRL_CMD_BIF_RESET=0x800102`

## Результат ограниченного поиска

В pinned ELF exact DWORD `0x800102`, если непосредственно закодирован как x86 little-endian immediate, дал **0 сырых совпадений** во всех четырёх секциях с `SHF_EXECINSTR`: `.init`, `.plt`, `.text` и `.fini`. Поэтому для этого шаблона нет адреса инструкции-кандидата и нет содержащего её FDE. Результат не получен из выборки функций: сканер проверяет точные байты `02 01 80 00` по полным executable sections, а при наличии совпадения находит FDE и декодирует его для проверки, что DWORD — непосредственный операнд инструкции.

Это ограниченный отрицательный результат только для прямой contiguous DWORD-immediate формы. Он не исключает вызов generic RM-control API с command ID из аргумента/таблицы/JS, построение значения из нескольких операций, или иной indirect/control-table путь. Не делался широкий whole-text disassembly для поиска произвольных dataflow цепочек.

## Что установлено о public command table и что остаётся неизвестным

Предыдущий pinned source report связывает header ID `0x800102` с MODS control-table handler `0x1fe0110` и его строкой журнала `deviceCtrlCmdBifReset_IMPL`. Это доказывает зарегистрированную handler-запись и обработку её входного `flags` в handler-е; такая таблица не является native callsite, непосредственно передающим literal `0x800102`. Данный скан не выявил native MODS wrapper/frontend/test caller, который в executable инструкции загружает этот literal.

Поэтому этот bounded source search не определяет, какой frontend/test wrapper подаёт public command в MODS, и не доказывает, делает ли этот wrapper повторную инициализацию после `SW_RESET`. Отдельная внешняя FDE `0x1fe0110` и её вызов BIF `+0x2f0` остаются обработчиком/внутренним dispatch-путём; автоматически считать её верхнеуровневым caller или reinit wrapper нельзя. IFR проверяет public script wrapper отдельно.

## Метод и pins

- ELF: `evidence_20261022_photo_followup/mods520_112/mods.unpacked`, SHA-256 `b3df4ccda11851601ffb91bcf251f42db2f8e74df66a1dd497b83c527d5fd2ca`.
- Public handler binding: `evidence_20261031_cmp02_x16/bif_reset_public_binding_01/RESULT_RU.md`, SHA-256 `9ed973196e84fd9bb99174dff82be5a3a425af860e833ff9a79a01c1bd1e5ee4`.
- Handler caller context: `evidence_20261031_cmp02_x16/bif2f0_caller_phase_01/RESULT.md`, SHA-256 `4d6593d3888c5c751e149c4426d5736fd9c7c6afe56b5a5fe69b209e8cd2e49c`.
- `SCAN.json` records the exact section set/ranges and zero candidate count. `replay.py` rechecks all pins and reruns this finite byte scan.

No RM-control command was issued; no SSH, live process, GPU, MMIO, reset, or hardware operation was performed.
