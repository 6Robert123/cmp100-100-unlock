> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# CMP 100-100: отчёт по сессии — Gen2 x1 и 4-й HBM-стек (26-27 окт 2026)

Статусы: **[ИЗМЕРЕНО]** сырой вывод; **[ВЫВОД]** интерпретация;
**[ИЗ ДОКУМЕНТА]** из отчёта/архива; **[НЕИЗВЕСТНО]** данных нет.

Железо (сверено по BDF/device/UUID перед каждым опытом):
- Основной lab **RESEARCH-HOST**: `01:00.0` CMP `15c2` (GPU-ID-REDACTED), `02:00.0` P100 `15f8`
  (GPU-ID-REDACTED), `03:00.0` CMP `15c2` (GPU-ID-REDACTED, целевая). Ядро 7.0.0-30, NVRM 580.178.04.
- **SECONDARY-HOST** (SECONDARY-HOST, PRIVATE-IP, user REDACTED): `01:00.0` CMP (GPU-ID-REDACTED),
  `02:00.0` CMP (GPU-ID-REDACTED). Ядро 7.0.0-38, NVRM 580.178.04. Обе чистые, 6144 МиБ, Gen1 x1.
[Local access configuration omitted.]

Все опыты read-only, кроме явно помеченных записью (с подтверждением); карты целы во всех
прогонах (0 новых Xid/AER, восстановление чистое). ROM-флэш НЕ выполнялся.

---

## ЧАСТЬ 1. Gen2 x1

### 1.1 Нативный путь смены скорости (офлайн, RM ELF `ea81a70…`)
- **Исполнитель `_nv017102rm`** (`0x135770`): штатная state-machine на `0x8c040`
  (poll `[4:0]`, поле скорости `[19:18]`, write-verify). Целится в Gen2 верно. [ИЗМЕРЕНО]
- **Gate режима:** `_nv017102rm` режим 4 гейтится флагом `BIF+0x956` (`cmpb $0,0x956; je exit`). [ИЗМЕРЕНО]
- **Производитель флага `_nv017812rm`** (`0x11f890`): распаковывает битовую маску `edx` →
  bit2→`+954`, bit3→`+955`, bit4→`+956`. [ИЗМЕРЕНО]
- **Политика `_nv017279rm`** (`0x141980`): маска = **AND пяти capability-источников**
  (HAL-слоты `[obj+0x1a8/1b0/1b8/1c0/1c8]`, функции `_nv0573xx`). [ИЗМЕРЕНО]
- **Producer `_nv017281rm`** (GP100 ordinal-29): при nibble `0x8c1c0[23:20]≥2`
  безусловно зануляет `+954/+955` (байт-пруф `je 0x12c1d6 → movw $0,0x954`). [ИЗМЕРЕНО]

### 1.2 Корень — фьюз (измерено live, двумя путями)
`0x2157c` BOOT_GEN23_DISABLE и `0x21580` BOOT_GEN3_DISABLE: **CMP=1/1, P100=0/0**
(kernel ioremap + userspace resource0 mmap — совпали). Каскад: LnkCap2 `0x02` (Gen1-only)
vs P100 `0x0e`; `0x8c1c0[23:20]`=2 vs 0; CYA `0x8841c` CMP `0xc0b46500` vs P100 `0x00b40500`. [ИЗМЕРЕНО]

### 1.3 Опыты записи (все обратимые, с подтверждением)
| опыт | что | результат |
|---|---|---|
| poke `0x8c1c0` (зеркало) | write 0x00010036 | **не прилипло** (readback=оригинал); HwInit/зеркало |
| Stage A: CYA-clear `0x8841c` | `0xc0346500→0x00340500` | **прилипло**, но LnkCap2 остался `0x02` (не лифт) |
| Stage B: CYA + LnkCtl2 Gen2 + retrain upstream | target=2, 3 retrain | линк остался **Gen1** |
| fuse-OPT `0x2157c` write 0 (fuse-clock ungated) | HAL-путь | **не прилипло** (readback=1); OPT read-only из host |

Офлайн: путь записи `fuseWrite_GP100` (`0x1403c70`) — обычный `writel` через `ungate(0x1373a0 bit3)→write→restore`, **без FUSECTRL/program-voltage** → обратимый OPT-override, НЕ OTP-burn. [ИЗМЕРЕНО]

### 1.4 falcon + PLM
- Фабричный falcon (`CMP_FACTORY`, `P100`): **нигде не пишет `0x2157c`/`0x21580`**; читает `0x21584`,
  пишет только PLM `0x88ab4` (`and 0xffffff8f`). `0x2157c/21580` в falcon не упоминаются
  (только host-MODS читает). [ИЗМЕРЕНО]
- **PLM `0x88ab4` = `0x8f` на ВСЕХ картах** (CMP01/03, P100, SECONDARY-HOST×2) — не различитель Gen1↔Gen3. [ИЗМЕРЕНО]

### 1.5 Формулировка вердикта Gen2 (с ревизией форка)
- **[ВЫВОД]** В разобранном коде (host RM580/MODS367 + falcon CMP_FACTORY/P100) **нет писателя
  gen-OPT** `0x2157c`/`0x21580`; host-запись дропается. НЕ доказано, что писателя нет нигде:
  BootROM/secure-boot-до-falcon/FWSEC/GFW не разбирались.
- **[ИЗ ДОКУМЕНТА, форк 2026-10-27 — ВАЖНАЯ РЕВИЗИЯ]** На **170HX** `0x2157c` тоже неписабелен и =1,
  **но Gen2 там РАБОТАЕТ с фьюзом=1**. Значит фьюз, вероятно, **НЕ жёсткий блокер**, а реальный
  барьер — **priv-ring / PLM**. 170HX = другая архитектура (GA100); переносимость на GP100 (Pascal)
  **[НЕИЗВЕСТНО]**, требует проверки. Это **переоткрывает** Gen2, который мы «закрыли».
- Исчерпано измерением на CMP100: ROM/IFR-флэш (IFR идентичен P100), рантайм-poke зеркал,
  CYA-clear, CYA+retrain, fuse-OPT write из host. Вне досягаемости: fuse-burn (необратимо),
  JTAG/HULK (нужен ключ), secure/GFW-путь (не разобран).

---

## ЧАСТЬ 2. 4-й HBM-стек (12→16 ГБ)

### 2.1 Гейтинг
`OPT_FBIO 0x21c14` → devinit COPY_NV_REG → runtime `CTRL_FBIO 0x21938` → FBPA-floorsweep.
12×512 МиБ = 6144 МиБ активно; 4 FBPA (из 16) тёмные. [ИЗ ДОКУМЕНТА + ИЗМЕРЕНО]
Нибблы фьюза различаются по картам (CMP03 `0x0f`, CMP01 `0xf0`, SECONDARY-HOST-02 `0xf000`) — floorswept
разные физические FBPA, итог один (4 тёмные). [ИЗМЕРЕНО]

### 2.2 Ключевой результат — `0x21938` писабелен (GREEN)
[ИЗМЕРЕНО, SECONDARY-HOST-01, reversible self-restoring]:
```
PRE   ctrl_fbio=0x03  opt_fbio=0x0f  opt_fbp=0x03
WRITE ctrl_fbio 0x03 -> 0  readback=0x00  took=1   ← override ПРИНЯТ
RESTORE 0x03  restored_ok=1
```
**[ВЫВОД]** CTRL_FBIO `0x21938` — писабельный runtime-регистр (в отличие от gen-фьюза `0x2157c`,
который дропал запись). Config-override floorsweep достижим. Это GO-условие из `gp100_ctrl_fbio_probe.c`.
**Оговорка:** live-poke НЕ ретренит память (тренировка на devinit) → `took=1` necessary, не sufficient;
фьюз `0x21c14` может ещё hard-gate'ить FBPA физически. Настоящий тест — override ДО тренировки.

### 2.3 Почему MODS floorsweep сейчас не запустить
- На основном lab MODS-на-CMP гоняют через интрикатный **syscall-gate v33** (GDB/MI ptrace);
  **подтверждённого MATS-прогона, видящего память, НЕТ** (искал маркеры — ноль). [ИЗМЕРЕНО по логам]
- **Исходника MODS.ko нет** (только .ko под 7.0.0-30). SECONDARY-HOST: ядро 7.0.0-38, **нет gcc/make** (apt есть). [ИЗМЕРЕНО]
- **[ВЫВОД]** «Портировать MODS на SECONDARY-HOST» = заново решить нерешённую задачу: тулчейн + исходник MODS.ko
  + перенос всего gate. Большой объём, низкий шанс.

### 2.4 Пути к ответу «встаёт ли 16 ГБ»
- **MODS `-floorsweep fbio_enable:0xf`** — чистый механизм (override до тренировки), но не запускается (2.3).
- **nouveau-патч** (open-source, правим sами floorsweep до memory-init) — тракнее, но риск неполного
  HBM2-init на GP100. Нужен тулчейн.
- **Flash VBIOS** (снять gate в devinit) — **NO-GO**: nvflash не читает карту, внешнего SPI-программатора
  нет, recovery-сети нет → необратимо. [ИЗ ДОКУМЕНТА]

---

## ЧАСТЬ 3. Сводная таблица регистров (live)

| рег | CMP03(осн) | CMP01(осн) | P100(осн) | SECONDARY-HOST-01 | SECONDARY-HOST-02 |
|---|---|---|---|---|---|
| `0x88ab4` PLM | 0x8f | 0x8f | 0x8f | 0x8f | 0x8f |
| `0x2157c` GEN23_DIS | 1 | 1 | 0 | 1 | 1 |
| `0x21580` GEN3_DIS | 1 | 1 | 0 | 1 | 1 |
| `0x21250` gen3-gate | 1 | 1 | 0 | 1 | 1 |
| `0x21c14` OPT_FBIO | 0x0f | 0xf0 | 0x00 | 0x0f | 0xf000 |
| `0x21938` CTRL_FBIO | 0x03 | — | — | 0x03 (writable!) | 0xc0 |
| `0x88084` LnkCap | 0x00453c11 | 0x00453c11 | 0x00454103 | 0x00453c11 | 0x01453c11 |
| `0x880a4` LnkCap2 | 0x02 | 0x02 | 0x0e | 0x02 | 0x02 |
| `0x8841c` CYA | 0xc0b46500 | 0xc0b46500 | 0x00b40500 | — | — |

---

## ЧАСТЬ 4. Открытые вопросы и что обдумать

1. **Gen2 переоткрыт форком.** Если барьер — PLM/priv-ring, а не фьюз (как на 170HX), то на GP100
   надо: разобрать, какой PLM/priv-уровень гейтит запись в gen-цепь, и достижим ли он (secure/falcon).
   [НЕИЗВЕСТНО] переносимо ли 170HX-наблюдение на Pascal. **Это самая многообещающая зацепка.**
2. **4-й стек:** `took=1` — config-override принимается. Нужен devinit-time тест (MODS или nouveau).
   MODS сейчас нереалистичен; nouveau — тракнее, но риск HBM2. Вопрос приоритета/усилий.
3. **Cousin ROM (cmp50/cmp170)** скопированы, не разобраны — референс методологии анлока (другая арх).
4. SECONDARY-HOST — рабочая безопасная площадка (2 чистые CMP100-100) для будущих опытов.

## Рекомендация (к обдумыванию, не действие)
Самый высокий ожидаемый возврат — **разобрать Gen2/PLM-зацепку форка** (офлайн, read-only, может
переоткрыть Gen2), прежде чем вкладываться в тяжёлый MODS-порт для 4-го стека.

## Файлы-доказательства (в evidence_20261026_gen2x1/)
`native_speed_gate/`, `cap_register_readout/`, `fuse_override_probe/`, `plm_sweep/`,
`gen3_stage_a/`, `fourth_stack/`, `cousin_roms/`. Память: `gen2-lock-otp-fuse.md`,
`fourth-stack-ctrl-fbio.md`. Форк: session `local_8a60bca0…` (idle).

---

## ОБНОВЛЕНИЕ (2026-10-27, позже): 4-й стек — ВЕТКА ЗАКРЫТА

После отчёта выше выполнен тест пути B (nouveau) на SECONDARY-HOST:
- Собран stock nouveau под 7.0.0-38, reboot SECONDARY-HOST в nouveau (обе CMP).
- [ИЗМЕРЕНО] nouveau читает тот же **6144 МиБ** ("unknown memory type", НЕ ретренит);
  ACR/GR-init падает: `pmu(acr) boot failed -5`, `gr init failed -5`, `timeout`.
  → nouveau на GP100 не тренирует память и не поднимает движок. **Путь B мёртв.**
- Инцидент: reboot в nouveau подвесил boot SECONDARY-HOST на ~12 мин (GR-timeout на консольной
  GP100 стопорит загрузку до сети). Машина поднялась сама; откат на nvidia чистый
  (обе карты nvidia/6144/Gen1/idle, 0 Xid/AER, конфиги восстановлены).
  Урок: **SECONDARY-HOST в nouveau не ребутить.**

**ИТОГ 4-го стека — софт-пути исчерпаны измерением:**
- CTRL_FBIO `0x21938` писабелен (`took=1`) — config-override принимается, НО live-poke не ретренит.
- nouveau (B) — не тренирует HBM2 на GP100, движок не поднимается. Мёртв.
- MODS (A) — нереалистичен (нерешённый syscall-gate, нет MATS-успеха, нет исходника .ko).
- Flash VBIOS — NO-GO без внешнего SPI-программатора + recovery-сети.
Остаётся только аппаратный путь (внешний CH341A + VBIOS devinit-мод), вне текущих средств.
**Ветка 4-го стека закрыта. Карты целы, SECONDARY-HOST в штатном nvidia.**
