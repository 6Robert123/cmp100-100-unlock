> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Фактический post-MMIO GET-only capture (run-001)

Дата bundle: `2026-10-26T11:34:26Z`–`11:34:33Z`. Это единственный стандартный RM `BUS_GET_INFO_V2` для CMP03 (`0000:03:00.0`, UUID `GPU-REDACTED`) с активным наблюдателем на post-handler leaf. Запрос завершился `ioctl_rc=0`, `errno=0`, `rm_status=0`; elapsed `73,754 ns`. `result.json`: operation/getter/cleanup rc=0, модуль выгружен; режим явно `GET-only`, без speed setter/PCI writes.

## Наблюдение и crosswalk

`capture.txt` META: stopped=1, registered=0, owner_tgid=17025, captured=stored=9/9, capacity=4096, overflow=0, kprobe_missed=0, cutoff_rejected=0. Все девять чтений принадлежат PID/TGID 17025 и одному context. Их времена `6652596042567`–`6652596080561 ns` попадают внутрь GET interval `6652596013093`–`6652596086847 ns`.

| BAR0 offset | чтений | значение | Офлайн crosswalk |
|---|---:|---:|---|
| `0x88084` | 2 | `0x00453c11` | PASS, `BAR_CROSSWALK_88084.json` |
| `0x88088` | 4 | `0x10110140` | PASS, `BAR_CROSSWALK_88088.json` |
| `0x88610` | 3 | `0x00000000` | PASS, `BAR_CROSSWALK_88610.json` |

Три аудита используют один capture, свежие run-001 `vmalloc.before/after` и sysfs resource CMP03. Для каждого выбранного события вычисленный physical address совпадает с `BAR0 start + offset`; стабильная ioremap привязана к BAR0 `0xf1000000..0xf1ffffff` (16 MiB). VA берётся из `effective_virtual`; leaf pin подтверждает инструкцию `leaf+0x37`, bytes `44 8b 24 98` (`mov r12d,[rax+rbx*4]`). У vmalloc reservation хвост 4096 байт оставлен неклассифицированным и не переводился в BAR-адрес.

## Здоровье и предел результата

До/после bundle snapshots: три GPU сохранили те же UUID/BDF, объёмы 12/16/12 GiB, used=0 MiB и link codes; CMP03 остался `1/1`. CMP03 BAR resource before/after совпал; leaf address/64-byte prefix/target instruction совпали. До/после `tainted` равно `12289`; список `probes` до/после пуст, `tracer` до/после `nop`; cleanup подтвердил модуль отсутствующим. В сохранённом post-run dmesg поиск не нашёл Xid/AER/PCIe Bus Error строк; отдельного dmesg-before snapshot нет, поэтому это только post-run отсутствие совпадений, не доказанный журнал дельты. В dmesg имеется обычная строка о неподписанном observer module, соответствующая taint snapshot.

Это доказывает путь чтения и VA→physical BAR crosswalk для трёх регистров в данном GET. Захват содержит **0 SET2/SET1 операций** и не устанавливает, что Gen2 достигнут: CMP03 всё ещё сообщает Gen1 x1. Нет вывода о вызванном RM write, retrain, устойчивости или поведении firmware.

## Повторение локального аудита

Без обращения к устройству:

```sh
for offset in 0x88084 0x88088 0x88610; do
  python3 evidence_20261026_gen2x1/active_leaf_observer/audit_virtual_bar_crosswalk.py \
    --vmalloc-before evidence_20261026_gen2x1/active_leaf_observer/post_mmio/run-001/vmalloc.before \
    --vmalloc-after evidence_20261026_gen2x1/active_leaf_observer/post_mmio/run-001/vmalloc.after \
    --resource evidence_20261026_gen2x1/active_leaf_observer/post_mmio/run-001/resource-0000:03:00.0.txt \
    --capture evidence_20261026_gen2x1/active_leaf_observer/post_mmio/run-001/capture.txt \
    --offset "$offset"
done
```

## Ключевые pins

SHA-256: capture `b893b22e369db16ec4a2c638665d622c4a321203d14f6aac24ce9ee8911c48f3`; getter log `f2ddd6a0707b893ae7f47aff4bcc7de30d7d49f25c899df3040a4c5e0d8a92c8`; result `440b07dc727dfbc4f353bc2d55bfbfcdf4c9a578b60ec4a5fc570bf0774951c2`. vmalloc before/after `e07b1900b91acc15287813f24faaff514e5ad45a48a82a0351fa39894042c78f` / `10d24bfd83a3d5653bf80ecbd8906c49ddd71c5ea677a62884d7b40fd0fecee6`; resource `5ef4408450a33a96828df1ad180829bbeee1378100edc38004b320f22e951b7a`; leaf pin `7fa867c10713494115181b2ae303662e698a0b296094fc1de72ce502e13b7a82`. Каждый crosswalk JSON содержит полный набор input hashes и адресное покрытие.
