> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Прямой kprobe API: disabled registration на RM read-leaf

26 октября 2026. Предыдущий goal-этап классифицируется как **progress**: получены новые pinned GP100/GV100 firmware и доказаны ABI-различия, изменившие план переноса. Этот этап также дал новый фактический результат: один disabled kprobe принят работающим ядром, без аппаратного опыта Gen2.

## Результат

Target `_nv049612rm [nvidia]`, entry `0xffffffffc17877e0`. На работающем `7.0.0-30-generic` передан `KPROBE_FLAG_DISABLED` в `register_kprobe()`. **registration_rc=0**, вызван `unregister_kprobe()` до возврата module init, `removed=Y`; модуль затем выгружен. Wrapper exit0, operation0, cleanup0, `module_present=false`: result.json (archive reference: `run-001/result.json`; original context may be excluded). Активных handlers и вызова enable в исходнике нет.

Это конкретно снимает прежнюю неопределённость «отказывает ли underlying kernel API на этом символе», но **не доказывает работоспособность активного обработчика, kretprobe, полноту записи событий или возможность Gen2**. Нового SET2, retrain, PCI/BAR write, driver reload, firmware replacement, PLM/OTP изменений не было. Единственная исполняемая модификация ядра — штатная загрузка/выгрузка минимального GPL diagnostic module с публичным kprobe API.

Прежние неудачные tracefs-регистрации были на других функциях `_nv049786rm` и `_nv017105rm`; их отказ `notrace` остаётся фактом. Результат здесь не переименовывает их в успех и не доказывает допустимость активных probes на всех NVIDIA functions.

## Почему этот метод отличался от прежнего

Просмотрены exact upstream Linux v7.0 `kernel/trace/trace_kprobe.c`, `kernel/kprobes.c`, `include/linux/kprobes.h`; URL/SHA в source manifest (archive reference: `source/MANIFEST.json`; original context may be excluded). `__register_trace_kprobe()` проверяет `within_notrace_func()` до вызова underlying API. В `register_kprobe()` этой frontend-проверки нет, но остаются его собственные address/blacklist/module проверки. Disabled flag сохраняется; `__register_kprobe()` не вызывает `arm_kprobe()` при disabled. Это анализ upstream, а успешная регистрация в running kernel — отдельное измерение конкретного случая.

Использование прямого API не отменяло core blacklist, lockdown или проверку адреса. Preflight показал lockdown `[none]`; никакие настройки защиты не менялись. Код не использует symbol-address lookup hacks, запись kernel memory или изменение инструкций вручную: адрес разрешал сам kprobe API.

## Подготовка и проверка

- Исходник (archive reference: `gen2_probe_feas.c`; original context may be excluded) SHA `82e5dfffb30f219317ecf972574e8ddb1bb118e3beb4c19a696dd60ab5aad520`; stack `struct kprobe` существует до синхронного unregister; no handlers/enable/GPU APIs.
- Модуль (archive reference: `gen2_probe_feas.ko`; original context may be excluded) SHA `edf2548e28fe9b29908a46b585baa4d3735b5b20a74b3a8af29a492729f62322`, собран headers текущего ядра, vermagic `7.0.0-30-generic SMP preempt mod_unload modversions`. Выводы сборки сохранены. Предупреждение compiler-name относится к одинаковой версии GCC13.3.0 с разным executable prefix; BTF не построен из-за отсутствия vmlinux, build exit0.
- 16 байтов live read-leaf побайтно совпали с pinned RM ELF: `f30f1efa41554989f54154534883ed10`, до и после одинаковы. Первоначальный reader остановился на своей неверной проверке длины15 вместо16; его source сохранён в `read_leaf_ro_v1_failed.py`, проба тогда не регистрировалась. Исправленный readonly reader отработал до module run.
- Ревью (archive reference: `REVIEW_RU.md`; original context may be excluded) источника выполнено отдельной проверкой. В runner исправлена найденная проверка alias-адресов: перед запуском проверяются не только имя, но все адреса существующих probes в диапазоне функции0x78. В фактическом before list нет probes. Это snapshot, не атомарная блокировка действий других администраторов.
- Runner (archive reference: `run_disabled_feasibility.sh`; original context may be excluded) SHA `123296b92319304ae12ac959dc0675f5ebc857c044ce75bb2f0155382b09ecc8`: source/module/helper pins, target module version, nop tracer, UUID/BDF/12–16–12GiB/used0/links preflight; единственный insmod; sysfs status; trap rmmod; pre/post comparisons. Повторять этот feasibility test без новой гипотезы не нужно.

## Фактические постусловия

`run-001/probes.before == probes.after` (оба пусты), tracer `nop` до/после, `leaf.before.json == leaf.after.json`, GPU CSV до/после совпадают. Независимая SSH-проверка после wrapper: independent_postcheck.txt (archive reference: `independent_postcheck.txt`; original context may be excluded), модуль отсутствует, probes отсутствуют, tracer nop, UUID/профили/used0 и links прежние. Не заявляется сравнение всего состояния ядра/всех services: измерены перечисленные поля.

## Следующий различитель для Gen2 x1

Подготовить отдельно **ограниченный активный observer** read-leaf для offset `0x8c040` (low5 completion) и `0x88088` (LinkSta), с фильтрацией/идентификацией целевого вызова, фиксированным buffer и учётом missed/overflow. Сначала проверить correctness handler/ABI и его сбор на безопасной наблюдаемой операции; только после этого один пересмотренный SET2 может дать новую информацию вместо прежнего повторения. Известная ABI read-leaf: input offset EDX, result EAX; RSI — map context, но mapping к конкретному GPU и runtime selection должны быть отдельно закреплены. Зафиксированный прямой disabled registration сам по себе не разрешает считать любую callback implementation безопасной.

Параллельный ACR путь остаётся офлайн: штатный GP100 loader принимает `data_size=0xc00` из host descriptor, но enlarged-image acceptance/GP100-specific writer ещё не доказаны. Приоритет следующего шага теперь — информативное наблюдение RM через проверенный kernel API, а не слепое повторение GV100 writes.
