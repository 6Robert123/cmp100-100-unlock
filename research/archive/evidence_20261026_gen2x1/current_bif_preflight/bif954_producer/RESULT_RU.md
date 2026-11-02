> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Кто производит BIF+0x954 в `_nv017283rm`: bounded offline audit

## Результат

Повторяемая проверка `audit_gp100_bif954_producer.py` (archive reference: `audit_gp100_bif954_producer.py`; original context may be excluded) завершилась `PASS`. Она заново декодирует инструкции из ELF через `objdump`, сверяет байты на выбранных VA и relocations; `audit.json` содержит машинно-читаемую выжимку. Это статическая семантика RM 580.178.04, не trace фактического вызова.

Ключевое уточнение для V3 кадра: на GP100 HAL ordinal 29 конструктор `_nv005948rm` сохраняет `_nv017282rm` в поле `+0x2b8` (`.text+0xb3fb83..b3fb8a`). Он читает HAL word из `[RSI+0x10]` (`+0xb3f8a3`) и делает group dispatch через `index>>5` (`+0xb3f8ce`); для ordinal 29 группа 0, после чего проверяется ordinal bit `EDX&31`. Перед assignment проверяется маска `0x1f000000` (`+0xb3fb62`), где bit 29 равен 0, поэтому эта ветка не пропускает его. Собственно assignment `_nv017282rm` разрешён следующей маской `0xe0000000` (`+0xb3fb74`), где bit 29 равен 1. Отдельная инструкция, которая могла бы заменить это поле на `_nv017283rm`, находится по `.text+0xb40731..b40738` и разрешена только битами `0x09f8ffe0`; bit 29 там **нулевой**. В соответствующей ветви слот остаётся `_nv017282rm`; его pinned relocation `.text+0x1329a2` ведёт в `_nv017281rm`. Поэтому V3 `a34/a38=1/5, +954/+955=0/0` нельзя объяснять как результат `_nv017283rm` на GP100.

## Контракт byte producer в `_nv017283rm`

* Начальный локальный кандидат для BIF+954 — bit 0 результата `_nv053907rm(GPU+0x4120, ECX=0x88610)`: `AND 1` и store в `[RBP+0xf]` (`.text+0x1331ca..1d7`). При `BIF+0xa34 != 0` управление идёт в декодер `+0xa38`; поле `a38[1:0]==1` ставит `[RBP+0xf]=1` по `+0x1339e0`. Для `a38=5` также ставится локальный EBX-кандидат в `+0x133a01`.
* До чтения `0x8c1c0` проверяются BIF byte `+0x959` и `+0x95a`. Если `+0x959!=0`, путь через `+0x1335a8` **всегда** очищает `[RBP+0xf]` в `+0x1335c1`, даже когда `+0x95a==0`; если `+0x959==0` и `+0x95a!=0`, путь `+0x1335cb..5dc` тоже очищает его. Значит `a34/a38` сами по себе не гарантируют +954=1.
* Затем читается `0x8c1c0`, `(value>>20)&0xf`. Nibble 2 уходит на `+0x133830`: +955 становится 0, а +954 получает `[RBP+0xf]` (`+0x133854..865`). Nibble 3 идёт через `+0x133497` и зануляет слово +954/+955. Nibble 0/1 используют кандидата EBX и вызов виртуального слота `OBJBIF+0x930`; обе последовательности записи (`+0x1332c8`, `+0x1336f7`) передают в +954 тот же stack candidate. Return +0x930 выбирает последовательность, но не меняет байт-кандидат.
* Indirect calls `OBJBIF+0x618` и `+0x620` получают `(RDI=GPU, RSI=OBJBIF)`; ненулевой return очищает EBX или R14D, не `[RBP+0xf]`. В ветке nibble 2 вызывается `+0x930`, но его return не проверяется до записи +954. CYA `0x8841c` и дальнейшие записи в `0x880a8/0x8c1c0/0x8c2c0` расположены после этих stores и не являются источником значения байта +954.

Все прямые stores, декодированные в функции, подтверждены по VA: `+0x1332c8`, `+0x133497` (zero word), `+0x1336f7`, `+0x133865`. Поэтому для **этой функции** различитель 0/1 — это совместно локальный кандидат (первоначальный `0x88610[0]` либо override от `a34/a38`), clear gates `+959/+95a` и выбранная nibble ветка `0x8c1c0[23:20]`. Этот контракт условен на вход в эту функцию; audit не доказывает runtime вызов или отсутствие более позднего writer в другой функции.

## Что фактически установлено и следующий различитель

В V3 global5 kcore (archive reference: `../actual/cmp03-global5-bif-preflight-20261026T120424Z-22074/global5-kcore.json`; original context may be excluded) CMP03 позже прочитан как `a34/a38=1/5`, `+954/+955=0/0`; захват не содержит `0x8c1c0`, `+959`, `+95a` или trace вызова callback. Он подтверждает состояние кэша в момент чтения, а не producer/dispatch.

Следующий полезный шаг — offline source audit выбранного GP100 `_nv017281rm` path, а затем только при необходимости existing read-only observer window с same-phase `0x8c1c0` и BIF `+0x959/+0x95a` на CMP03. Не повторять global5 preflight и не посылать SET ради этого вопроса. Без фактической трассы call edge и этих фазовых значений причина runtime `0/0` остаётся неразличённой.

## Воспроизведение и pins

Из корня workspace:

```sh
python3 evidence_20261026_gen2x1/current_bif_preflight/bif954_producer/audit_gp100_bif954_producer.py > evidence_20261026_gen2x1/current_bif_preflight/bif954_producer/audit.json
```

* ELF: `evidence_20261022_photo_followup/rm580_generic_offline/nvidia580-analysis.ko`, SHA-256 `ea81a7018398a59c53299c80c9b80276b45405d24f566e66f32430cf9bcb1237`.
* Исходный сохранённый дизассемблер: `exact_gp100_cap_init_objdump.txt`, SHA-256 `96b4b155ad202833c0a147494bc71fb09532c1f62761c5f9f397fe6bce99f5c4`.
* Связка HAL ordinal 29 → `_nv017282rm` → `_nv017281rm`: `RM580_GEN2_DISPATCH_CORRECTION.txt` (archive reference: `../../../evidence_20261022_photo_followup/rm_pcie_speed_cmp03/RM580_GEN2_DISPATCH_CORRECTION.txt`; original context may be excluded); новый скрипт проверяет сами ELF relocations для ctor/thunk и raw bytes соответствующих branch/store sites. Он намеренно не заявляет, что static constructor proof является runtime call trace.
