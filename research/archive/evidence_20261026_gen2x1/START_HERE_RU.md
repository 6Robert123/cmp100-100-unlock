> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

> 30 октября 05:58 +11:00: сервер вернулся после ручной перезагрузки, все3CMP12GiB/Gen1x1, CUDA copy CMP02 PASS. x16 пока не достигнут. Актуальное состояние (archive reference: `../evidence_20261029_cmp02_x16/START_HERE_RU.md`; original context may be excluded).

> Последнее состояние29октября18:48 UTC: после NVFlash read init завершился timeout (память12/6/6GiB). Разрешённая оператором reboot-команда принята, SSH пока недоступен; новое здоровье GPU не подтверждено. Нижеследующее успешное восстановление — историческое. Текущее состояние (archive reference: `../evidence_20261029_cmp02_x16/reboot_recovery_20261029/RESULT_RU.md`; original context may be excluded).

> 29 октября, актуализация x16: одна новая width command2 дала sticky bit4 и Gen1x1. CMP02 восстановлена target-only bus reset с прежним12GiB профилем, без server reboot. Все3GPU доступны, CUDA1MiB copy CMP02 прошёл. Цель x16 не достигнута. Текущий аппаратный отчёт (archive reference: `../evidence_20261029_cmp02_x16/WIDTH_COMMAND2_RESULT_RU.md`; original context may be excluded).

# CMP03: устойчивый Gen2 x1 — рабочий этап 26 октября 2026

**Обновление29октября:** оператор установил распаянную прежнюю карту№3 (`ID-REDACTED…`, serialBOARD-SERIAL-REDACTED) вместо P100 в02:00.0 и запросил x16 без прошивки. Сейчас все три GPU — CMP12GiB. Старые P100/16GiB и boot/pGpu/ioremap assumptions устарели. Проверенные root retrain, native SET_WIDTH16 (RM0x56) и88084 masked write x16 (не принята) ширину не подняли. Отдельная экспериментальная горячая запись8c040 high5=16 сорвала связь; BAR-откат был недоступен, recovery retrain не помог. После **явно разрешённой оператором перезагрузки** все три карты и профили восстановлены, Gen1x1; новый boot `UUID-REDACTED`. Повторять горячие width0/width16 записи без нового обоснования нельзя. SPI не меняли; x16 и Gen2 не достигнуты. [Актуальные доказательства и восстановление](../evidence_20261029_cmp02_x16/XPCFG16_RESULT_RU.md).

Историческая цель этапа 26 октября: **устойчивый PCIe Gen2 x1**, с использованием первичных данных сообщества GV100. На 29 октября текущая цель изменена оператором: **x16 на распаянной CMP02 без прошивки**, см. актуальное состояние выше. Дальнейший текст этого документа описывает прежний Gen2-этап; его команды не являются текущим планом для CMP02. Gen2 пока не получен. Выполнены GET-калибровки observer и временный штатный driver-policy опыт с восстановлением. Существующий init при этом сбрасывал CMP, перезагружал драйвер и восстанавливал memory profile; native speed SET, root TLS/retrain и flash не вызывались. Последняя развилка: global5 загрузился, но BIF Gen2 flag остался0; подробности в конце этого документа.

Целевая карта: CMP03, BDF `0000:03:00.0`, UUID `GPU-REDACTED`, SPI family `_2` с SHA `8ae75ee4d1fc28e7c5e42982d16f726f40017b731ac046474e5207e82bb406ba`. Исходные SPI и привязка по serial находятся в предыдущем приёме (archive reference: `../evidence_20261026_spi_intake/START_HERE_RU.md`; original context may be excluded).

## Новые результаты

- Проверен актуальный primary community repo GV100: `mintoriakamoto/CMP100-210xFULLTENSORSxGEN3x1`, main `8abaad2fe2903c8ea10fe88c04c2acc8778aebf2`, tree совпадает с прежним pinned checkout. Новая PR5 касается HBM clock offset, не PCIe. Отдельный свежий checkout/API snapshots сохранены; прежний checkout не изменён. Отчёт с permalink (archive reference: `community_gv100/GV100_COMMUNITY_UPDATE_RU.md`; original context may be excluded).
- Существенная граница старых опытов: GP100 post-bind `0x88610=0x1001` **не тождественен** GV100 fallback `0x88610=1`. В последнем bit12=0, после него отдельный custom ACR payload пишет `0x8872c=6`, затем выполняется root/XP speed sequence. Полного GP100 аналога этой цепи пока нет. При этом отсутствие прямого literal `0x8872c` в RM ELF не означает отсутствия computed writer/регистра/прав записи. Разбор последовательности (archive reference: `gp100_sequence/SEQUENCE_BOUND_RU.md`; original context may be excluded).
- Повторно использовать IFR `0x1ec` как «speed-only» нельзя: сохранённая поправка (archive reference: `../evidence_20261022_photo_followup/GEN23_BIT27_WIDTH_CORRECTION_20261022.txt`; original context may be excluded) связывает `[31:27]` у `0x8c040` с шириной. Эта запись сохраняет rate bits `[19:18]` и задаёт upper field=1. Для Gen2 x1 сохраняем весь upper field. Старый `IFR_SPEED_ONLY_MODEL_RESULT_RU.md` имеет исторически неверную постановку, даже если его арифметика верна. Строгий GP100 dispatch к MODS width constructor не полностью доказан; это не основание повторять опасный zero-width опыт.
- Получены реальные **stock ACR files с файловой системы lab**, только чтение/копирование/распаковка. Ничего не загружалось на GPU. GP100 `ucode_load.bin`: 9632B, SHA `81f3f97490ee8b9b725169d9e9c85ebea782f30c0481064a39d50addf164da73`; GP100 `bl.bin`: 832B, SHA `6b7b484b343a3a1ed58072893cb491bd97b5fd5e380acbfc50a1970cba77d766`. GV100 `ucode_load.bin`: 18688B, SHA `b349f0355548716770531eb9082544bb84de059f17bbf3952fb09dd27d8673d4`, **точно совпадает с community input pin**. Полные compressed/decoded SHA и provenance — firmware manifest (archive reference: `firmware_inventory/MANIFEST.json`; original context may be excluded). Это пригодные входы для проверки бинарных предпосылок переносимости вместо предположения по совпадению PCI ID или адреса.
- На этих входах уже выполнен ACR portability audit (archive reference: `gp100_sequence/acr_portability/GP100_GV100_ACR_PORTABILITY_RU.md`; original context may be excluded): GP100 legacy magic `0x3b1d14f0`/прямые patch values против GV100 `0x10de`/patch-pointer indirection; `gm200_acr_hsfw_load_bld` v1=76B против `gp108_acr_hsfw_load_bld` v2=84B. Простой retarget community hook на GM200 с прежним чтением21DWORD захватил бы8байт сверх v1; без retarget GP108-probe не наблюдает выбранный GP100 callback вообще. GP100 выбирает PMU ACR path, не следует называть его GV100 SEC2 path. Root отдельно проверил packed source layout, общий offset72 поля data_size и точное равенство четырёх lab ACR copies первичным control blobs — source ABI audit (archive reference: `acr_source_abi_audit.json`; original context may be excluded). Реальная совместимость payload bytecode/privileged completion пока не установлена.

## Что извлечено из сохранённой GP100/P100 трассы

Новый streaming audit (archive reference: `audit_saved_pcie_trace.py`; original context may be excluded) проверил 120-MB v4 trace по прежнему SHA, фактическим BAR0 resources и активным наблюдаемым MAP. Выделены 109 обращений к13 PCIe targets; full/partial DWORD coverage, raw строки и номера сохранены в JSON (archive reference: `saved_pcie_trace_audit.json`; original context may be excluded). Для всех сохранённых R/W есть подходящие MAP, relevant UNKNOWN0. **Terminal Lost60 остаётся; отсутствие записи в сохранённых строках не доказывает отсутствие writer.** Это warm reload, не SET2, cold POST или firmware-internal observation.

| Регистр | CMP01/CMP03 | P100 | Допустимый вывод |
|---|---|---|---|
| `0x2157c / 0x21580` | `1 / 1` | `0 / 0` | GV100 pre-POST Gen3 guard отверг бы этот вход; физический OTP и идентичность GP100/GV100 семантики этим не доказаны |
| `0x88610` | `0` | `0x1001` | Сохранённая stock policy различается |
| `0x8872c` | `0` | `0` | По одному сохранённому чтению на карту; не доказательство отсутствия latched/internal effects |
| `0x8c040` | `0x08089000` | `0x80009000` | XOR `0x88080000`, bits19/27/31; rate и width нельзя смешивать |
| `0x8c2c0` | `0x068711b3` | `0x060711b2` | XOR `0x00800001`, bits0/23; bit2=0 уже у обеих, перенос Ampere DIS_G2-clear не даёт здесь изменения |
| `0x88088` | `0x10110140` | `0x11030140` | Сохранённые LinkSta Gen1x1 / Gen3x16 |

Указанные значения не являются fresh live-read этого этапа; время исходного захвата `2026-10-26T04:17:32Z`. Raw evidence и replay остаются на прежних местах.

## Что считать достижением цели

Перед будущим ограниченным аппаратным опытом — сверка текущих UUID/BDF/driver/profile и закреплённых executed artifacts. Обязательные критерии результата:

1. Реальные **endpoint и upstream LinkSta** одновременно показывают speed code2 (5GT/s), width1. Advertised LnkCap/`nvidia-smi max` сами по себе не являются результатом: на GV100 community live link сохраняется выше повторно ограниченного advertised cap. В PCIe v1 LinkSta определён; unsupported endpoint LnkCtl2 не добавляется ради этой проверки.
2. Согласование сохраняется в ограниченном тестовом окне: первоначально600с с1Hz телеметрией, включая idle и проверенный H2D/D2H workload с проверкой возвращённых данных. Это выбранный операционный критерий, не универсальная гарантия стабильности.
3. Нет новых Xid/uncorrectable AER/link drop; corrected AER сравниваются с baseline. Недоступные counters означают неполную проверку, не нули. Сохранены статусы workload, операции и финального здоровья отдельно.
4. В конце корректные UUID/BDF, прежний configured12GiB профиль и ожидаемое состояние driver/services. Успешное recovery не переименовывает failed/incomplete unlock в успех.

Выполнены проверки переносимости, runtime GET-калибровки и штатный policy опыт; критерии выше ещё **не выполнялись на Gen2 CMP03**. Прошиваемый ROM/custom GP100 payload не выпущен.

## Воспроизведение

```sh
python3 evidence_20261026_gen2x1/audit_saved_pcie_trace.py
python3 evidence_20261026_gen2x1/gp100_sequence/audit_gp100_sequence.py
python3 evidence_20261026_gen2x1/ifr_speed_semantics/audit_ifr_speed_semantics_sources_20261026.py
python3 evidence_20261026_gen2x1/gp100_sequence/acr_portability/audit_acr_portability.py
python3 evidence_20261026_gen2x1/audit_acr_source_abi.py
```

IFR source audit использует закреплённый текущий extraction JSON: если его формат/байты меняются, сначала пересмотреть pin, а не молча заменить hash. Stock firmware input copies read-only; `firmware_inventory/decompress_preserved_firmware.py` использует `zstd -dc` на lab через stdin только для распаковки уже сохранённых compressed bytes.

## Продолжение goal: новый фактический диагностический путь

Предыдущий этап был progress, и этот этап дал новые результаты, меняющие следующий шаг:

1. RM580 (archive reference: `consumer_8872c/RM580_8872C_STATIC_CONSUMER_BOUND_RU.md`; original context may be excluded) и MODS367/400 (archive reference: `mods_8872c/MODS_8872C_CONSUMER_BOUND_RU.md`; original context may be excluded): найденные immediate `0x72c` относятся к eDP DPCD backlight capabilities (точное совпадение MIN/MAX полей и bit packing с первичным NVIDIA header), а не к PCIe BAR0+`0x8872c`. Это закрытый ложный след. Общий RM MMIO helper умеет вычислять `0x88000+offset`, но target-producing callsite ещё не установлен.
2. GP100 ACR code (archive reference: `gp100_acr_code/GP100_ACR_LOADER_BOUND_RU.md`; original context may be excluded): raw сегменты декодированы; host data_size0xc00 проходит до v1 DWORD18/byte0x48. Приём enlarged size secure consumer не доказан. В ходе root review исправлена адресная модель BL: `start_tag0x100 <<8` даёт VMA **0x10000**, не0x100. BL cave relative0x170 соответствует file0x1b0/VMA0x10170;12 нулевых байтов существуют, но это не доказательство исполнимого GP100 write payload. Старые неверно базированные disassembly не использовать.
3. PC attribution (archive reference: `pc_attribution/TRACE_PC_BOUND_RU.md`; original context may be excluded): текущий builtin `mmiotrace.trace_pc` реально0, permissions0; это measured kernel metadata/byte, не вывод только из исходника. GRUB и ядро не менялись.
4. **[Фактическая disabled kprobe registration](direct_kprobe_feasibility/RESULT_RU.md) прошла на `_nv049612rm [nvidia]`** через публичный direct API. Источник модуля отдельно reviewed, собран под текущие headers, target live16bytes совпали с pinned ELF. После единственного register/unregister модуль выгружен; wrapper original0/operation0/cleanup0, независимый postcheck: probes отсутствуют, tracer nop, UUID/память/used0/links прежние12/16/12GiB и1x1/3x16/1x1. Active handler/SET/retrain при этом не выполнялись.

Следующий различитель на момент disabled-registration (ниже уже выполнен) — отдельно проверенный активный observer для RM read-leaf (`EDX=offset`, `EAX=result`) с target identity/owner filtering, bounded buffer и учётом missed/overflow; затем новая информативная фиксация low5 `0x8c040` и LinkSta `0x88088` внутри одного пересмотренного SET2. Один лишь успех disabled registration не доказывает безопасность или полноту такого observer. До доказанного наблюдения очередной прежний SET2 не повторять. Это приоритетнее дальнейшего слепого переноса GV100 writes; GP100 ACR остаётся параллельным офлайн-путём. **Цель Gen2 x1 не достигнута и остаётся активной.**

### Milestone: post-MMIO GET-only read capture

В выполненном run-001 активный observer записал девять чтений одного `BUS_GET_INFO_V2` и завершился чисто. Офлайн crosswalk отдельно PASS для CMP03 BAR0 offsets `0x88084`, `0x88088`, `0x88610`; счётчики capture полны, owner и GET interval совпали. [Результат, hashes и ограничения](active_leaf_observer/post_mmio/RESULT_RU.md). Это подтверждает наблюдаемость чтений по BAR, но capture не содержит SET2/SET1 и CMP03 остаётся Gen1 x1. Gen2 не достигнут; setter не является частью этой калибровки.

### Свежая развилка: штатный global5 не разрешает Gen2

После observer-калибровок выполнен отдельный свежий kcore-only snapshot: stock BIF +954/+955=0/0 и +a34/+a38=0/0; [raw и разбор](current_bif_preflight/RESULT_CURRENT_RU.md). Затем **фактически выполнен** reviewed V3 policy preflight: временный штатный `RMPcieLinkSpeed=5`, переинициализация существующего 12GiB профиля, kcore-only snapshot, обязательное восстановление. Raw: `current_bif_preflight/actual/cmp03-global5-bif-preflight-20261026T120424Z-22074/`. В override получено **+a34/+a38=1/5, но +954/+955=0/0**, +9a8/+9ac=0/0. Следовательно, одного стандартного global5 недостаточно для достижения исторического флага Gen2=1; прежний SET2 пока не повторять. Имеющийся bounded SET2 helper отдельно собран и reviewed, но fixed no-argument setter не запускался.

V3 exit0, original_error=null, cleanup_errors=[], временный config удалён; params и health до/после совпадают. Независимый postcheck: все UUID доступны, 12/16/12GiB, used0, link1x1/3x16/1x1, службы init/monitor/display active, probes пусты, tracer nop. Штатный init **сам сбрасывал CMP, перезагружал драйвер и выполнял прежний memory profile helper**; это не GET-only опыт целиком. Native SET2/SET1/root TLS/retrain/payload/flash не вызывались. Новый реальный cold boot не выполнялся: `coldboot_fresh` в job receipt означает свежий запуск существующего PCI collector при warm reinit.

**Исправленный producer path:** статический GP100 ordinal29 выбирает `_nv017282rm → _nv017281rm`, а не `_nv017283rm`. В [проверенном GP100 path](current_bif_preflight/gp100_281/RESULT_RU.md) nibble `0x8c1c0[23:20] >=2` безусловно зануляет +954/+955; для nibble0/1 существенны кандидаты от `0x88610`/override и clear bytes +959/+95a. Это source semantics, не runtime trace V3. Старый primary `global5_pre_unbind.json` имел nibble2 до SBR/DI/CYA подготовки, а перед историческим SET2 — nibble1/+954=1; не смешивать фазы. Следующий шаг — сопоставить exact baselines и подготовить единственный информативный SET2 с новым observer только после доказанной подготовки; простого global5 недостаточно. Сравнить с community GV100 early-init route, не переносить значения/ACR ABI вслепую. После двух profile reload старые pGpu/context/ioremap VA — исторические; перед будущим observer заново проверить текущий module/leaf bytes/BAR mapping/GET calibration. Gen2 x1 всё ещё не достигнут; цель активна.

В качестве reference для минимальной подготовки важен исторический **first-gate postbind**, до последующих ручных записей: BAR nibble1, `0x88610=0`, CYA `0xc0b44500`, рядом kcore +954/+955=1/0. Поздний `pre_native_set2` уже содержит `0x88610=0x1001` и CYA `0x00b40500`: это другая фаза. Не переносить эти две записи и не запускать старый wrapper целиком. Исторические снимки и timestamps сопоставлены отдельно в producer report; это согласование фаз, не доказательство причины runtime V3. [Выполненный policy опыт и replay](current_bif_preflight/RESULT_GLOBAL5_RU.md).
