> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Native SET_PCIE_LINK_WIDTH на GP100/CMP

## Вывод

Для активного RM **580.178.04** родной native `SET_PCIE_LINK_WIDTH` на GP100/CMP **не является рабочим путём запроса x16**: статическая таблица HAL для `HAL_IMPL_GP100` (ordinal 29) устанавливает BIF `+0x460` в `_nv017768rm`, и этот callback сразу возвращает `0x56` (`NV_ERR_NOT_SUPPORTED`). По этому handler path ширина `16` не отправляется к тренингу; таймаут или аппаратное recovery для этого статического маршрута не ожидаются. Сам control dispatcher не содержит поля таймаута и отдельного cancel/recovery механизма.

При этом общий handler/helper **не отсекает запрос только потому, что запрошенная ширина выше прочитанного max**: он отдельно допускает значения 1, 2, 4, 8, 16 как допустимые значения и передаёт их implementation callback. У GP100 callback — unsupported stub, так что принятие значения generic-слоем не означает возможность смены ширины. Это вывод для pinned RM 580.178.04 binary, совпадающего с версией в сохранённом `dmesg`; не обобщается на другие драйверы/HAL.

Есть и независимая аппаратная граница в **текущей CMP на `02:00.0`**: сохранённый baseline показывает endpoint LnkCap `0x453c11`, максимальная ширина x1, тогда как root port `00:02.0` объявляет x16. Поэтому стандартный root-port retrain, уже выполненный владельцем, ожидаемо оставил link x1; endpoint не предлагает x16. Снимок после операции остаётся Gen1 x1 на обеих сторонах (`LnkSta 0x1011` endpoint, `0x3011` root), без training/pending, карта здорова. Идентичность этой карты: UUID `GPU-REDACTED`; не переносить на прежнюю CMP/P100 на другом BDF.

**Решение по software x16:** для именно этой карты и локального RM 580.178.04 не посылать native SET_PCIE_LINK_WIDTH как следующий путь. Текущий endpoint maximum x1 — самостоятельная причина, а GP100 native callback в установленной таблице всё равно заканчивается `NV_ERR_NOT_SUPPORTED`. Это статический вывод, аппаратный SET не выполнялся.

## Статический путь в pinned RM

Локальный объект `nvidia580-analysis.ko` — ELF64 x86-64, SHA-256 `ea81a7018398a59c53299c80c9b80276b45405d24f566e66f32430cf9bcb1237`, размер 121,989,128 байт. Идентичность содержимого совпадает с локальным pinned copy в `rm580_generic_offline`; сохранённый live `dmesg` текущего CMP02 называет RM `580.178.04`.

1. Ранее сохранённая dispatch-коррекция идентифицирует `0x20801804` как `SET_PCIE_LINK_WIDTH`, params size 8 bytes, handler `_nv057463rm` по `.text+0x128bd0`. `RM580_GEN2_DISPATCH_CORRECTION.txt` (archive reference: `../../evidence_20261022_photo_followup/rm_pcie_speed_cmp03/RM580_GEN2_DISPATCH_CORRECTION.txt`; original context may be excluded) отдельно оговаривает исправление старого неверного descriptor mapping. Локальный исходный `ctrl2080bus.h` не приложен; это вторичная локальная запись о сверке descriptor с официальным header, а не самостоятельная копия header. В raw handler сохраняется params pointer, zeroes `params+4` только если размер больше 7, затем передаёт control в `_nv017630rm`; отсутствующий implementation object даёт `0x56`.
2. `_nv017630rm` (`.text+0x11ff80`) сначала вызывает BIF callback slot `+0x230` и extracts `(returned_value >> 20) & 0x3f`. Запрошенная ширина из params сравнивается с этим значением; при несовпадении helper отвергает только ненулевые значения, которые не являются степенью двойки в диапазоне 1…16. Он **не делает `requested <= returned_value` check**. Следовательно, ширина 16 может пройти generic validation даже при значении из getter, равном 1, но затем всё ещё зависит от implementation slot `+0x460`.
3. Для HAL ordinal 29 локальный сохранённый audit связывает GP100 с `_nv017282rm` через `_nv005948rm` (`.text+0xb3fb8a`) и `_nv017281rm` (`.text+0x1329a2`). В constructor selection region `.text+0xb401c0`, slot `+0x460` выбирается маской `0x09f8ffef`. Бит 29 равен нулю (`mask & (1<<29) == 0`), поэтому для ordinal 29 сохраняется адрес `.text+0xb47200`, `_nv017768rm`.
4. `_nv017768rm` по `.text+0xb47200` состоит из `mov eax, 0x56; ret`. Значит валидный request width=16 проходит общую проверку, попадает в callback и возвращается как `NV_ERR_NOT_SUPPORTED`, без вызова link-width training action в этой ветви.

Сохранённые адресные окна и bytes:

- `handler_128bd0.disasm.txt` (archive reference: `handler_128bd0.disasm.txt`; original context may be excluded)
- `generic_helper_11ff80.disasm.txt` (archive reference: `generic_helper_11ff80.disasm.txt`; original context may be excluded)
- `hal_ordinal29_selection.disasm.txt` (archive reference: `hal_ordinal29_selection.disasm.txt`; original context may be excluded)
- `bif_slot460_selection.disasm.txt` (archive reference: `bif_slot460_selection.disasm.txt`; original context may be excluded)
- `gp100_slot460_stub.disasm.txt` (archive reference: `gp100_slot460_stub.disasm.txt`; original context may be excluded)
- `RM580_GEN2_DISPATCH_CORRECTION.txt` (archive reference: `../../evidence_20261022_photo_followup/rm_pcie_speed_cmp03/RM580_GEN2_DISPATCH_CORRECTION.txt`; original context may be excluded) — prior HAL ordinal mapping and command descriptor summary.

## Источники/границы для текущей карты

- `baseline.txt` (archive reference: `../baseline.txt`; original context may be excluded): PCIe endpoint `02:00.0` LnkCap x1 / LnkSta x1; upstream `00:02.0` LnkCap x16 / LnkSta x1.
- `retrain.jsonl` (archive reference: `../retrain.jsonl`; original context may be excluded): один root-port RL/retrain, затем polls сохраняют endpoint x1 и root x1; никаких ошибок training/pending в сохранённых polls.
- `postcheck.txt` (archive reference: `../postcheck.txt`; original context may be excluded): UUID/BDF, 12 GiB, idle allocation, P-state/link-health summary.
- `dmesg.before.txt` (archive reference: `../dmesg.before.txt`; original context may be excluded): installed NVIDIA module version 580.178.04. Live binary SHA приведён выше.

Автор статического аудита API не запускал. После аудита родитель выполнил один native SET_PCIE_LINK_WIDTH=16; он вернул RM0x56. См. `../set_width16_operation_receipt.json` и `../set_width16_tail.txt`. Для vendor API здесь нельзя заявить фиксированный wall-clock timeout: у команды нет явно заданного в header времени ожидания, но pinned GP100 callback return path — immediate unsupported stub. Не выполнять иной native/driver path на основании этой статической сверки без отдельного определения его конкретного callback и recovery поведения.
