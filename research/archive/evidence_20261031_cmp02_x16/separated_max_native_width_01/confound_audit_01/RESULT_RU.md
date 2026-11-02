> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# MODS 520: `PMU Breakpoint` label versus separated-hook scalar returns

## Вывод

Поздняя строка separated run `Error 000000000838 : Global.InitializeGpuTests PMU Breakpoint [26.456 seconds]` не доказывает срабатывание PMU hardware breakpoint и не устанавливает причинность width/CYA writes или 20 s hold. Raw log содержит такие же `Global.u32 PMU Breakpoint` errors **до первой separated-run записи**.

На строках 242–253 каждое сообщение `Global.u32 ...` стоит непосредственно перед соответствующей baseline `SEP16 READ` и его описание меняется вместе со скаляром:

- `0x15c210de` → `Error 000000039838 ... PMU Breakpoint`;
- `0x00453c11` → `... Board does not match ...`;
- `0x08089000` → `... NVDEC6 error`;
- `0xe0000195` → `... unmapped error`;
- `0x0200a043` → `... File argument not valid`;
- `0x10110142` → `... SMBPBI test message (silent)`.

Те же value-dependent ошибки возникают на prewrite reads (строки 256–267), также до первой записи. В pinned separated hook строка 13 определяет `function u32(x){return x>>>0;}`, а `rd()` вызывает `u32(raw)` при нормализации каждого регистра (строки 15–20). После сообщения об ошибке hook продолжает печатать соответствующий read с `valid=true`. Сильнейшее объяснение этой корреляции: MODS JS bridge воспринимает ненулевой числовой return helper-функции как MODS status/error code. Значит слово `PMU Breakpoint` может быть всего лишь ошибочной интерпретацией скаляра `0x15c210de`; наличие `Global.InitializeGpuTests ... PMU Breakpoint` позже не подтверждает отдельное аппаратное событие. Это корреляционный вывод по логу и hook, а точный JS bridge contract в доступных исходниках не установлен.

## Сравнение с предыдущим init

Combined04 hook использует inline `>>>0`, не helper `u32()`, и его stdout не содержит таких `Error ... Global.u32` строк между register reads. Тот запуск завершился обычным `Error Code = 000000000000 (ok)` за 7.969 s. Но его command timeout ветка сразу выходила; success-only 20 s hold на строках 72–73 не исполнялся. Это лишь полезное сравнение реализаций hook, не доказательство причинности и не контроль busy-hold. `positive_control_route_06/old_normal_site_7472e6.txt` — статическое дизассемблирование, не отдельный live-run.

В separated run порядок фактов такой: stdout строки 242–267 — сообщения `Global.u32` при baseline/prewrite; 401 — command2 timeout; 405 — CYA restore `0xe0000195`; 407–408 — 20 s synchronous busy hold с `no_gpu_io=true`; 467 — поздняя `Global.InitializeGpuTests PMU Breakpoint [26.456 seconds]`; 475 — общий code label; 489 — MODS end за 27.814 s. `run_01/result.json` даёт `mods_rc=1`; runner wait отдельно завершился `returncode=0`, это не успех MODS.

В локальном MODS 520.112 ELF найдены строки `PMU Breakpoint`, `PMU Breakpoint (non-fatal)` и `Error Code = %08u (%s)`, но файл stripped. В доступных локальных материалах не найден исходный producer/checker; эти строки не показывают caller и не доказывают hardware-семантику.

## Следующий различающий шаг

Не запускать предложенный ранее no-width-write hold-control: при таком же ненулевом helper return он может печатать те же ложные PMU/status labels без width writes и не будет различать hold starvation от JS status pollution. Сначала требуется offline/CPU-only установить callback return contract или приготовить callback, где все вложенные helper calls возвращают только штатный `OK`/0, а данные сохраняются и возвращаются отдельно. Нужна проверка отсутствия ненулевых scalar returns в полном hook path. Только после неё можно обсуждать на отдельном чистом здоровом boot no-write hold-only контроль; он не подготовлен и не одобрен этим документом.

Здесь не выполнялись MODS runs, SSH, hardware access, writes, driver changes, reset/reboot или handoff.

## Источники, пины и воспроизводимость

Инспекция локальная. Относительные пути ниже отсчитываются от этой папки.

- `../execution_01/raw_capture/run_01/stdout.txt` — 29,129 bytes, SHA-256 `4afc3d89efbcab261a1f1ef0b161ebf36b8fed8cdcd5ec6a0d883fdc4fc76c20`; ключевые строки 18, 242–267, 401–408, 467, 475, 489.
- `../execution_01/raw_capture/run_01/result.json` — SHA-256 `35df82de2ec07c4643c0725d3baa288c6a824b893f90537f11c42b2f731c4c3a`; `mods_rc=1`.
- `../execution_01/raw_capture/launcher_execution_01/runner_wait_result.json` — SHA-256 `212adcda47ac45aa422e8e56c022540fbcbbd08d22b706a4741184f7c1f8bc57`; runner/controller `returncode=0`.
- `../after_init_combined_native_width.js` — SHA-256 `0fc723a9a01895de82f69837cb0cb995217129caa0928c9976393ac20e3b3f37`; helper at line 13, call in `rd()` lines 15–20.
- `../../combined_native_width_04/run_01/stdout.txt` — 17,068 bytes, SHA-256 `ad7d50f089ddee1c865eb5f351cdb491758121af29cec4685a226c1e11aa5002`; key lines 18, 72–73, 173, 240, 254. Hook/input hashes are listed in `../../combined_native_width_04/SOURCE_PINS.sha256`.
- `../../../evidence_20261022_photo_followup/mods520_112/mods.original` — 83,747,712 bytes, SHA-256 `9aaef3ecdfb0cee492628ed211675283292fa6d2206efa808b5fcab0cb407ec6` (pinned packed MODS 520.112 input).
- `../../../evidence_20261022_photo_followup/mods520_112/mods.unpacked` — 255,426,008 bytes, SHA-256 `b3df4ccda11851601ffb91bcf251f42db2f8e74df66a1dd497b83c527d5fd2ca` (stripped ELF). Reproduction command: `strings -t x ../../../evidence_20261022_photo_followup/mods520_112/mods.unpacked | rg 'PMU Breakpoint|InitializeGpuTests|u32 unmapped error|Error Code|Global\\.'`. `strings -t x` gives file offsets: `PMU Breakpoint` at `0x4693357`, `PMU Breakpoint (non-fatal)` at `0x4739b70`, and `Error Code = %08u (%s)` at `0x46ec9a5`. All are in PT_LOAD #2 (`p_offset=0`, `p_vaddr=0x400000`), so their mapped VAs are respectively `0x4a93357`, `0x4b39b70`, and `0x4aec9a5`. `InitializeGpuTests` is from runtime output, not this static inventory.
- `../../positive_control_route_06/old_normal_site_7472e6.txt` — SHA-256 `a8e68dae294d5c8f7e8cc5bc37578a1bdb4e0f7119dc667c5d56a3a4074b6e67`, static disassembly only.
