> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Результат bounded global5 BIF preflight

## Итог

Сохранённые материалы подтверждают завершённый цикл временного глобального параметра NVIDIA `RMPcieLinkSpeed=5`, одного read-only снимка BIF через `/proc/kcore`, возврата к stock-параметрам и восстановления сервисов. При global5 снимке BIF CMP03 был `+0x954/+0x955=0/0`, `+0xa34/+0xa38=1/5`, `+0x9a8/+0x9ac=0/0`, GPU `+0x5420=0`. Это наблюдаемая комбинация; она показывает, что `RMPcieLinkSpeed=5` не сопровождался переходом пары `+0x954/+0x955` в `1/0`. Она не доказывает причинный механизм и не проверяет Gen2.

SET2, прямые BAR/MMIO и PCI config writes, reset/retrain из самого runner не выполнялись. При этом это **не** полностью безмутационный запуск: runner временно создал modprobe policy, а перезапуск закреплённого `cmp100-12gb.service` наследует существующий init с unbind/reload драйвера, CMP reset и записью профиля helper-ом. Именно эта стадия использовалась для применения и возврата параметра.

## Подтверждение запуска и восстановления

- `APPLY_V3.stdout` содержит финальный маркер `GET_ONLY_GLOBAL5_CAPTURED` и подпись `0/0, 1/5`; stderr пуст. В `completion.json` `original_error=null`, `cleanup_errors=[]`, `config_absent=true`, число прямых вызовов SET/BAR/config/reset/retrain равно 0. Это согласуется с успешным return 0 runner-а; отдельный файл с кодом выхода процесса в пакете не сохранён.
- В `params-before.bin` и `params-after.bin` побайтно одинаковый SHA-256 `672b0916…`. Единственное отличие `params-global5.txt` — `RegistryDwords: ""` заменено на `RegistryDwords: "RMPcieLinkSpeed=5"`; `RegistryDwordsPerDevice` остаётся пустым. `modprobe-registry-before.json` равен `[]`, а временный config отсутствует в completion record.
- Сервисные состояния до и после совпадают: `cmp100-12gb=active`, `cmp100-temp-monitor=active`, `display-manager=active`, `cmp100-coldboot-pci-capture=inactive`. История содержит две завершённые успешные операции restart профиля: применение global5 и последующее восстановление stock. Обе сопровождаются свежим успешным запуском capture unit; транзакционные очереди завершились пустыми.
- Это fresh systemd capture/restart при том же boot ID `UUID-REDACTED`, а не холодная перезагрузка хоста. До/после dmesg образуют точный префикс из 1241 строки и 96 добавленных строк; видны повторные загрузки/выгрузки NVIDIA в рамках profile reinitialization. В добавленных строках есть DRM сообщения `No compatible format found` и `Cannot find any crtc or sizes`; их не классифицирую как причину сбоя. Не найдено `Xid`, `AER:`, `PCIe Bus Error`, `Completion Timeout` или `RmInitAdapter failed` в добавленной части.
- Три health-снимка (`before`, `global5`, `after`) побайтно совпадают: обе CMP 12 GiB остаются Gen1 x1, P100 16 GiB — Gen3 x16; у каждой memory-used равно 0. Независимый postcheck дополнительно совпадает с post-run health, четырьмя полями tracer/services и полным восстановленным `/proc/driver/nvidia/params`.

## Точность и пределы

Reader `d40c7eb3…` на pinned driver `580.178.04` с boot ID выше сообщает O_RDONLY `PROC_KCORE`, без resource0/BAR/MMIO. В `global5-kcore.json` первая и повторная выборки идентичны; UUID/BDF/GPU ID CMP03 совпадают с pinned mapping. Снимок — два равных прочтения, не непрерывная трасса. Health postcheck — отдельная проверка профилей; он сам по себе не измеряет BIF.

Не делается вывод о безопасном GET для `0x8c040`, о необходимом условии этого регистра, о Gen2 readiness или о том, что global5 должен устанавливать `+0x954`. Gen2 не тестировался. Условия для будущего SET требуют отдельного reviewed preflight.

## Воспроизводимые pins

Полный машинно-читаемый аудит и хэши файлов: AUDIT_GLOBAL5.json (archive reference: `AUDIT_GLOBAL5.json`; original context may be excluded). Фактический run package находится в actual/cmp03-global5-bif-preflight-20261026T120424Z-22074 (archive reference: `actual/cmp03-global5-bif-preflight-20261026T120424Z-22074`; original context may be excluded); исходный архив — actual-global5-run.tar (archive reference: `actual-global5-run.tar`; original context may be excluded). Главные pins: runner v3 SHA-256 `b6ba648e…`, reader SHA-256 `d40c7eb3…`, archive SHA-256 `e7a5da7c…`, dmesg before `8db42b98…`, dmesg after `ae14d700…`, independent postcheck `96e664e8…`.

## Дополнительное чтение после возврата stock

После stock reinitialization сохранён отдельный повторный kcore-read `stock-after.stdout.json` с тем же boot ID и драйвером. Повторные поля идентичны; свежая подпись CMP03: `+0x954/+0x955=0/0`, `+0xa34/+0xa38=0/0`, `+0x9a8/+0x9ac=0/0`, `+0x5420=0`. Адреса объектов теперь `objGPU=0xffff8d4c8c6e0008`, `objBIF=0xffff8d4c82e4e008`; они отличаются от сохранённых для global5. Старые object pointers нельзя считать актуальными после reinitialization.

`leaf-after.txt` содержит отдельный 64-byte read-only kcore-предпросмотр `_nv049612rm`: `address=0xffffffffc17877e0`, а bytes по смещению `+0x37` равны `448b2498`. Поле helper SHA `ea1dd7b0…` совпадает с source pin `read_leaf_instruction_ro.py` в `active_leaf_observer/post_mmio/BUILD_PINS.txt`. Это проверка текущего загруженного кода по префиксу, не активный MMIO observer и не доказательство, что выбранная инструкция выполнилась.

Тот же файл содержит sysfs resource row `0xf1000000–0xf1ffffff`; BDF `0000:03:00.0` / BAR0 дополнительно совпадает с PCI enumeration строкой в `dmesg.after.txt`. Это адресное соответствие ресурса, не чтение BAR содержимого. stderr обоих readback команд пуст; родитель сообщил, что этот post-reload сбор завершился с exit 0 и не загружал модули/не вызывал speed setter. Exit receipt помечен как родительская запись, а не встроенный лог процесса: PROCESS_EXIT_RECEIPT.json (archive reference: `PROCESS_EXIT_RECEIPT.json`; original context may be excluded).

Перепроигрываемые локальные проверки находятся в audit_global5.py (archive reference: `audit_global5.py`; original context may be excluded); последний запуск PASS, 50 проверок. Сводка и точные pins — AUDIT_GLOBAL5.json (archive reference: `AUDIT_GLOBAL5.json`; original context may be excluded); сохранённый вывод последнего replay — AUDIT_GLOBAL5_REPLAY.json (archive reference: `AUDIT_GLOBAL5_REPLAY.json`; original context may be excluded).
