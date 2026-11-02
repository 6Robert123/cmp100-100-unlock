> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Результат: conditional separated-MAX trial, run 01

Исполнена ровно одна разрешённая цепочка на boot `UUID-REDACTED`: staging → read-only helper preflight → MODS handoff → один runner. Новых попыток, retry, RL/LD/reset, rollback, NVIDIA restore или reboot не выполнялось. Состояние оставлено для parent review.

## Предпосылки и READY

Stager сообщил `STAGED_CANDIDATE_AND_WORKCOPY_VERIFIED_NO_HARDWARE`; исходная и скопированная 520.112 workcopy manifest совпадают (`95e0ee...a40e`), SPC (`507240...9ddb7`) и packed MODS (`9aaef3...07ec6`) совпадают, `run_01` отсутствовал до запуска.

Frozen helper `--preflight-only` вернул `READ_ONLY_PREFLIGHT_PASS`. Boot/kernel: `UUID-REDACTED` / `7.0.0-30-generic`; NVIDIA `580.178.04`; три ожидаемых UUID имели по 12288 MiB; Qwen inactive/MainPID 0; display-manager и cmp100-temp-monitor active. Свежий read-only paired sampler собрал 25 строк: endpoint cap/status `00453c11 / 1011`, root port `007a7903 / 3011`, Gen1 x1, без training.

Единственный `--execute-handoff` достиг `MODS520112_METADATA_HANDOFF_READY`, target 02:00.0 bound `mods`, hold GPUs 01/03 unbound с `cmp100-research-hold`, API `0x436`. Metadata helper sequence завершилась `result=PASS`, `token_acquired_at_exit=0`. Post-READY проверки: mods refcount 0, holders пусты, `/dev/mods` fuser rc 1 с пустым stdout/stderr, без mods/gdb/gdbserver PID или fd owner. Перед runner `run_01` отсутствовал.

## Один запуск и результат

Runner PID 5997, start ticks 204192; actual runner wait RC 0. MODS app PID 6006, start ticks 204310; bwrap PID 6004; sampler PID 6003. Два same-PID CPU snapshots, пустой `g_RegWrMask`, exact BDF, translation/identity/map проверки и свежий paired LinkSta guard прошли; gate был опубликован как `GO` только после этих проверок.

Hook записал строго ожидаемые пять фаз: `native_22_20_clear` (`0x8c040=0x08089000`), `cya29_clear` (`0x88150=0xc0000195`), `separate_max16_command0` (`0x8c040=0x80089000`), единственный `native_command2_once` (`0x8c040=0x80089002`), `cya29_terminal_restore` (`0x88150=0xe0000195`). MAX16 readback был точным; cap изменился с `0x00453c11` на `0x00453d01`; три разнесённые stability pairs подтвердили точные `0x80089000` и cap16. Endpoint до command2 оставался Gen1 x1 (`0x88088=0x10110142`), поэтому разрешённая единственная command2 была выдана.

Command2 immediate read и 40 bounded polls вернули `0x80089010`; hook сообщил `NATIVE_COMMAND status=TIMEOUT polls=40 window_ms=4000`. Из свежего валидного CYA слова hook выполнил один bit29 restore в `0xe0000195` и подтвердил точный readback. `CLEANUP_KNOWN` и 20s no-GPU-I/O hold завершились. MODS app завершился с RC 1; runner result: `NATIVE_COMMAND_TIMEOUT_CYA_CLEANUP_REQUIRES_REVIEW`.

Независимый 90s observer завершился RC 0 с 450 парными строками и без unknown row. В каждой строке endpoint и root port оставались Gen1 x1; финальные LinkSta `0x1011 / 0x3011`. Физический x16 не наблюдался и не сертифицирован. После завершения: fuser пуст, mods/gdb PID отсутствуют, mods refcount 0; target остаётся bound к mods, hold GPUs остаются unbound/held, Qwen inactive. Production NVIDIA/services восстановление **не** выполнялось.

## Источники

- Сырая stage receipt: `stage_receipt_remote.json`, helper stdout `stage_receipt_remote_stdout.json`.
- Read-only preflight stdout и numbered logs/events: `handoff_preflight_stdout.json`, `preflight_logs_01/`.
- Fresh baseline sampler: `preflight_linksta_01.jsonl` и `preflight_linksta_stdout.txt`.
- Полный handoff READY state/events/helper logs: `handoff_ready_01/`.
- Полный runner source stdout/stderr, snapshots, validations, 450-row sampler, result, before/after CAP/STA/AER, controller launch receipt и actual wait RC: `raw_capture/`.
- Post-run read-only owner/binding/service state: `post_run_ro_capture.json`.

Candidate source pins: hook `0fc723a9...c789f26`, runner `69d4d10a...2920652`, validator `05e9a069...a0c640`, stager `2bd1c562...63219c`. No further device action is authorized by this result record; preserve volatile state pending parent decision.

## Дополнение: PMU failure и recovery

Архивный `work/mods.log` уточняет вторую ошибку запуска: `Global.InitializeGpuTests PMU Breakpoint` (`000000000838`) на 26.456 s; строка также содержит `Global.u32 PMU Breakpoint` (`000000039838`). После честного terminal/FD audit выполнена одна разрешённая обычная graceful recovery reboot; SSH закрылся после вызова, marker подтвердил `reboot_count=1`, новый boot был проверен read-only. Подробный health/recovery capture — [RECOVERY_RESULT_RU.md](RECOVERY_RESULT_RU.md); автоматического restore/retry/CUDA действия не было.


### Корректировка интерпретации PMU label

См. [confound audit](../confound_audit_01/RESULT_RU.md). До первой native write raw stdout уже выдавал `Global.u32 PMU Breakpoint` рядом с baseline/prewrite read scalars; поэтому поздняя строка `Global.InitializeGpuTests PMU Breakpoint` не доказывает hardware PMU breakpoint и не устанавливает причинность. Точный контракт JS bridge не установлен. MODS app RC1 остаётся неизменным raw фактом; меняется только интерпретация текстового label.
