> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Полный CPU/GDB тест завершён — ПАУЗА

По решению оператора полный подготовленный CPU-only тест выполнен, после чего работа приостановлена. До явного resume ничего не запускать.

## Результат:4/4 PASS

| Case | HBP init/mode/return/API | Outcome |
|---|---|---|
| valid0 |1/1/1/2| mode0 snapshot, два one-shot перехода, естественный app exit0 |
| nonzero2 |1/1/0/0| mode2 остановлен, второй resume отвергнут |
| badmap |1/1/0/0| небезопасный snapshot отклонён, второй resume отвергнут |
| nonzero_init_return |1/1/1/1| native EAX7 обнаружен, процесс остановлен |

Во всех negative cases после проверки identity выполнен один явный terminate_after_review, затем terminal-only GDB quit, outer child exit0/PTy closed и shutdown. AppRC0 для убитых процессов не выводился. Все записанные supervisor/child PID отсутствуют. Parent сам повторно разобрал финальные raw всех4cases и проверил17 final evidence hashes.

Primary summary (archive reference: `gpu_reset_mode0_cpu_full_01/SUMMARY_RU.md`; original context may be excluded), parent independent raw/control/health review (archive reference: `gpu_reset_mode0_cpu_parent_review_01/RESULT_RU.md`; original context may be excluded), checksum list (archive reference: `gpu_reset_mode0_cpu_full_01/FINAL_EVIDENCE_SHA256SUMS`; original context may be excluded).

Важная поправка: при проверке поиск шёл requests.jsonl в неверном driver_run_01 и ошибочно сообщил об отсутствии. Parent read-only получил правильные work/gdb_control/requests.jsonl всех4sessions. В каждом ровно один continue_to_mode0 и один continue_after_mode0; в каждом negative case один terminate_after_review и один quit. Логи/хеши/операции: gpu_reset_mode0_cpu_parent_review_01/control_logs_correct_path_01 и REQUEST_COUNT_REVIEW.json. Тесты ради логов не повторялись.

## Сервер после теста

Boot9c18d01b-4338-4fad-96ba-e4a78e60850a. Все3 expected UUID/BDF под NVIDIA580.178.04,12288MiB, overrides null, Gen1x1; Qwen USER inactive/dead/MainPID0; display/temp-monitor active. mods не загружен, /dev/mods отсутствует. Финальный raw CSV самостоятельно проверен parent с strip headers/values: raw nvidia_exact=false был parser KeyError из-за пробелов, не ошибка NVIDIA. Источник: gpu_reset_mode0_cpu_full_01/final_health_01/SSH.stdout.raw. Никаких MODS/GPU/RM API, register writes, driver handoff или reboot в полном CPU тесте не было.

## Что это закрывает и что дальше

Подтверждена работа observer02/controller/cleanup в реальном Linux GDB с fixture-profile, который меняет только адреса/anchors/import-path и сохраняет AST policy. Это НЕ runtime proof GP100 init/reset, не x16 и не CUDA. Production адреса MODS не подменять CPU fixture адресами.

Источник observer02 GDB SHA fb70d3d2d46ff2490430f5f7193182cd3f2c346d9644a86a7242b4187769597e; core92e492a5cff3737d75a44cacd3ad075b1b3a765b0039bea5f90e4973444593fe; payload manifest7887bf11f68dff12fd9b8d332188ef52fa2e3cd0b8b2d039565cf200c0616750. CPU harness manifestbee1a3776e2bb6708005944d20b990cd9c67116be884a7bb1f56925cd912619c. Выполненные sources/raw не менять.

x16 пока НЕ достигнут; цель на паузе. После явного resume следующий этап — fresh health/manifest preflight и отдельно рассмотренный normal-MODS mode0 observer на target02 UUID GPU-REDACTED, чтобы снять ESI0 и actual reset-list f7a7fb8 vs GpuDevMgr f7a7fd0 до dispatcher. Это ещё НЕ выполнено. Никаких новых reset/width requests пока не назначено; SPI/VBIOS writes, OTP и protection bypass запрещены. Старые MAX/SetWidth/RL/LD пробы без новой предпосылки не повторять.

Предыдущая source карта и состояние до теста сохранены в HANDOFF_READY_BEFORE_TEST_20261031_RU.md и START_HISTORY_BEFORE_CPU_TEST_DONE_20261031.md. Старый valid0-01 был утрачен при ручном reboot; все текущие cases используют session-02 и завершены. Не использовать исторические PID/socket.
