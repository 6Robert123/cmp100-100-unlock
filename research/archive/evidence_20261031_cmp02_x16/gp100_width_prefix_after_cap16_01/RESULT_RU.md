> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# GP100 MODS 520 Set16: префикс до очистки base field

## Вывод

В GP100 callback `0x1fe9ca0` нет отдельной явной записи для clock, lane mask, power или ASLM до первой записи в selector `0x8c040`. До неё есть синхронизация/ожидание уже idle состояния `0x8c040[4:0]==0`, чтение selector `0x88` и условное формирование поля `[19:18]` в локальной копии слова `0x8c040`. Это конкретный native-шаг, которого нет в combined04 JS candidate как отдельного selector-read/conditional transform. Он может быть значим, если возвращаемый selector `0x88 & 0xf` не равен 1; имеющиеся артефакты не устанавливают его фактическое значение во время combined04. Поэтому это установленное отличие алгоритма, но не доказанная причина оставшегося `0x10`.

Для текущего observed baseline `0x8c040=0x08089000` selector case `1` оставил бы те же `[19:18]` после clear-and-set-bit19. Если case был `2`, native-копия получила бы `0x080c9000` (bit18); case `3` или default путь очистил бы оба бита. Следовательно, копировать native lower preparation точно без readback selector `0x88` нельзя заключить из одного baseline слова. Значение low nibble `0x88` и семантическая расшифровка этих mode bits здесь не доказаны.

В полном native request-16 пути последующие явные write requests таковы: (1) `0x8c040` с `[22:20]` очищенными и exact readback; (2) `0x88150` с bit29 очищенным; (3) `0x8c040` с low nibble заменённым на `2`. Затем ожидается чтение `0x8c040` с low five bits равными нулю. Это подтверждает три нижних регистрационных write requests, но не отдельное предварительное управление clock/lanes/power.

## Аргументы и ветви

В обёртке `0x1fea1f0` сохранённый вход `ECX` записан в `R12D`; непосредственно перед виртуальным вызовом `[RBX+0x528]` (VA `0x1fea244`) код делает `EDX=R12D`, `RDI=RBP` (receiver/context), `RSI=RBX`, `ECX=4`. Значит вызываемый target получает `EDX=requested width`, `ECX=4` как четвёртый integer argument. Сам target сравнивает `ECX` с 4 в `0x1fe9cac`; иной режим немедленно уходит на error ID `0x5cf`, return `0xffff` (`0x1fe9dc0–0x1fe9ddd`). Ветка width=16 подтверждается jump-table entry 16: table VA `0xa448b90`, byte offset `0xa408b90`; её qword target равен `0x1fe9e18`.

Перед dispatch target получает lock на объекте `RDI+0x1c50` через helper `0x1bc51e0`; далее вызывает helper `0x1bc5380` с selector/ID `0x5da`, читает `0x8c040` через `0x1bc1110`, тестирует `AL & 0x1f` и повторяет цикл при ненулевом результате (`0x1fe9cd5–0x1fe9d17`). Это idle precondition/loop, а не предзапись register. На combined04 исходное точное слово `0x08089000` уже имело low five bits zero; значит перед началом candidate write последовательности соответствовало loop-exit condition. Поздний observed readback `0x80089010` имеет low-five `0x10`, то есть native completion predicate не был выполнен при sampler чтении.

После idle loop virtual getter `[RDI+0x1a0]` читает selector `0x88` (`0x1fe9d19–0x1fe9d2d`); код извлекает и сохраняет младший nibble:

- nibble `2`: `0x1fea148–0x1fea15a`, clear `[19:18]`, set bit18;
- nibble `1`: `0x1fe9e00–0x1fe9e12`, clear `[19:18]`, set bit19;
- nibble `3`: fallthrough в `0x1fe9d9c`, clear `[19:18]`;
- nibble `0` или `4..15`: лог/diagnostic calls и затем тот же clear `[19:18]` на `0x1fe9d9c`.

Далее для width `16` jump table направляет исполнение в `0x1fe9e18`: локальная копия base `0x8c040` очищает bits `[22:20]` через `AND 0xff8fffff`; helper write `0x1bc09a0` отправляет её, helper read `0x1bc1110` немедленно сравнивает значение целиком. Несовпадение уходит на error ID `0x630` и return `0xffff`, до CYA/command writes. При совпадении код читает `0x88150`, очищает только bit29 (`AND 0xdfffffff`) и вызывает write helper; затем строит `0x8c040=(current & 0xfffffff0)|2`, записывает через тот же helper, получает wait helper result для `0x647` и повторно читает `0x8c040`. При `AL & 0x1f != 0` цикл повторяется. Условие успеха — ровно low5 zero; при helper result `0x65` выполняется timeout cleanup, устанавливающий bit29 в `0x88150`, затем return/error path. Readback ширины selector `0x88` выполняется лишь после успешного idle handshake.

Никакого прямого clock/lane/power helper call до `0x1fe9e18` в этом теле не найдено. Абстрактные helper `0x1bc09a0/0x1bc1110` обслуживают указанные selector reads/writes; здесь они не раскрывают bus mapping и не доказывают физический MMIO адрес. Selector `0x88` также не следует отождествлять с PCIe capability без отдельной mapping evidence.

## Сопоставление с combined04

Combined04 live bundle фиксирует baseline `0x8c040=0x08089000`, запись `0x08089000` (очистка `[22:20]`, readback прошёл), запись `0xc0000195` в `0x88150` (bit29 clear/readback прошёл), затем единственную запись `0x80089002` в `0x8c040`. Прочитано `0x80089010`; 40 polls/4 s не дали `(value & 0x1f)==0`, и эксперимент остановился без retry/CYA restore. Endpoint capability после опыта `0x00453d01` (reported max16), endpoint/root LinkSta остались `0x1011`/`0x3011` (Gen1 x1, training=false) во всех 450 парных строках. Это наблюдение не доказывает, что omitted selector branch породил low-five `0x10`, и не доказывает ни смысл поля `0x10`, ни PCIe policy lock.

Наиболее предметный следующий аналитический шаг — восстановить только read-only выход getter selector `0x88` на exact GP100, если он есть в имеющихся снимках/логах, и сравнить с candidate's assumed no-change `[19:18]`. В этой папке нет такого значения; нового live действия этот отчёт не задаёт.

## Воспроизводимость и ограничения

Исходник — pinned unpacked MODS 520.112 ELF SHA-256 `b3df4ccda11851601ffb91bcf251f42db2f8e74df66a1dd497b83c527d5fd2ca`. В RX `PT_LOAD` VA-to-file mapping: `file_offset=VA-0x400000`, поэтому entry `0x1fe9ca0` отображается на file offset `0x1fa9ca0`. FDE `0x1fe9ca0..0x1fea1ee` и полная свежая разборка сохранены рядом; `REPLAY.sh` проверяет SHA-256 и побайтное совпадение свежего `objdump` вывода. Функция `0x36711af320` не разбиралась: в выбранной ветви до base clear к ней нет call/reference, и для ответа о префиксе она не требуется.

Доказано статически по исходным байтам: аргументы callback, precondition loop, getter/branch, регистровые selector values, write/read order, timeout predicate и отсутствие явных иных предварительных write calls внутри FDE. Отдельно доказаны live результаты combined04 из его сохранённых bundle. Не доказаны фактический selector nibble в combined04, низкоуровневое значение этих полей, причина `0x10`, completion успешного native x16 или физическая тренировка link.
