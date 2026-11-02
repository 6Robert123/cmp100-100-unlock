> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# CMP 100-100 PCIe Gen2: итоговый отчёт сессии 2026-10-26/27

Цель: устойчивый PCIe Gen2 x1 на CMP03 (GP100), с опорой на первичные данные и
прецеденты сообщества (CMP 170HX/50HX). Все статусы — по правилам проекта:
[ИЗМЕРЕНО] / [ИЗ ДОКУМЕНТА] / [ВЫВОД] / [НЕИЗВЕСТНО].

## 0. Железо и метод

- Основной lab: `RESEARCH-HOST` (ssh REMOTE-HOST`), [access policy omitted]. Карты:
  `01:00.0` CMP01 `10de:15c2` BAR0 `0xf3000000`;
  `02:00.0` P100 `10de:15f8` BAR0 `0xf2000000`;
  `03:00.0` **CMP03 (цель)** `10de:15c2` UUID `GPU-REDACTED`
  BAR0 `0xf1000000`.
- SECONDARY-HOST: `SECONDARY-HOST` PRIVATE-IP (ssh `SECONDARY-HOST`). Две чистые CMP100-100 (01/02),
  6144 МиБ, Gen1 x1 — тот же фьюзованный SKU.
- Чтение: BAR0 `resource0` mmap + kernel ioremap-модуль + config-space `setpci` —
  минимум два независимых пути, совпали. Все модули non-resident (init → `-EAGAIN`).
- RM ELF pin: `nvidia580-analysis.ko` SHA `ea81a7018398a59c53299c80c9b80276b45405d24f566e66f32430cf9bcb1237`.
- boot_id прогонов: `ID-REDACTED…` (26.10), `ID-REDACTED…` (27.10) и др. — указаны в под-отчётах.

## 1. [ИЗМЕРЕНО] Состояние регистров CMP vs P100

| регистр | CMP01 | CMP03 | P100 | заметка |
|---|---|---|---|---|
| `0x2157c` OPT_GEN23 | 1 | 1 | 0 | коррелирует с Gen1 |
| `0x21580` OPT_GEN3 | 1 | 1 | 0 | коррелирует с Gen1 |
| `0x21250` gen3-validator-gate | 1 | 1 | 0 | GF117 validator |
| `0x88ab4` XVE PLM | 0x8f | 0x8f | 0x8f | одинаково (не различитель) |
| `0x88084` LnkCap | `0453c11` | `0453c11` | `0454103` | speed[3:0] 1↔3 |
| `0x880a4` LnkCap2 | 0x02 | 0x02 | 0x0e | supported vector |
| `0x880a8` LnkCtl2 | 0 | 0 | `1e0003` | target speed |
| `0x88088` LnkSta | Gen1x1 | Gen1x1 | Gen3x16 | текущий линк |
| `0x8841c` PRIV_MISC_1 | `c0b46500` | `c0b46500` | `00b40500` | CYA Gen2/3 override bits |
| `0x88610`/`0x8860c` | 0/`0800` | 0/`0800` | `1001`/`1801` | VSEC |
| `0x8c2c0` CYA_0 | `068711b3` | `068711b3` | `060711b2` | **bit2(DIS_G2)=0 у всех** |
| `0x8c040` LINK_CONFIG_0 | `08089000` | `08089000` | `80009000` | MAX_RATE[19:18] 2↔0 |
| `0x8c1c0` PL_LINK_RATE | `00210036` | `00210036` | `00030036` | nibble[23:20] 2↔0 |
| `0x8872c` XVE_OVR | 0 | 0 | 0 | одинаково |
| `0x21000` FUSECTRL-канд. | `c0040000` | `c0040000` | `c0040000` | одинаково |
| `0x88fe8-ff8` XVE PLM | BADF | BADF | BADF | priv-masked у всех |
| `0x8e1b0-bc` XP3G_PLM | BADF | BADF | BADF | priv-masked у всех |

Цель-критерий Gen2 x1: `0x88088 == 0x00210140` (speed=2, width=1). Не достигнут.

## 2. [ИЗМЕРЕНО] Выполненные опыты (все обратимые/read-only, карты целы, 0 Xid/AER)

1. **Статический разбор RM (ELF):** нативный speed-executor `_nv017102rm` (`0x8c040`,
   mode-4 gated by BIF `+0x956`); цепь флагов `_nv017812rm`(маска)→`_nv017279rm`(AND 5
   capability-источников `_nv0573xx`); producer `_nv017281rm` (nibble `0x8c1c0[23:20]≥2`
   → зануляет `+954/955`). Детали: `native_speed_gate/`.
2. **Host `0x8872c`:** писателя в RM нет (0 immediate; 2 DWORD только в `.rela.call_sites`).
   `consumer_8872c/`, host-разбор.
3. **IFR/ROM:** PCIe-записи IFR байт-идентичны CMP↔P100; скорость не задаёт ни одна;
   `0x8c040` off `0x1ec` = width. Флэш исключён (лок не в ROM).
4. **Запись `0x8c1c0` (с подтверждением):** не прилипла (HwInit mirror). `cap_write_probe/`.
5. **Stage A — CYA clear `0x8841c` (pre-POST):** запись прилипла (`c0346500→00340500`),
   но LnkCap2 остался `0x02` — не лифтит. `gen3_stage_a/`.
6. **Stage B — CYA clear + LnkCtl2 target Gen2 + retrain с upstream:** линк остался Gen1.
7. **Fuse-OPT `0x2157c=0` (self-restore):** отвергнуто (readback=1, took=0), даже с
   ungated fuse-clock. Путь записи — обратимый OPT-override (не OTP-burn, по дизасму
   `fuseWrite_GP100 0x1403c70`). `fuse_override_probe/`.
8. **PLM sweep:** `0x88ab4=0x8f` одинаков на всех; фьюз-различители CMP≠P100; falcon
   пишет `0x88ab4`, но gen-OPT `0x2157c`/`0x21580` не пишет нигде в разобранном коде.
9. **170HX-сравнение + механизм:** см. §3. `gen2_170hx_compare/`.

## 3. [ИЗ ДОКУМЕНТА] Прецедент 170HX и почему он не портируется

Источник: `cmp100-research/sources/cmp170hx/docs/unlock/pcie-gen2.md`, `how-it-works.md`,
`hardware/fuses-and-otp.md`.

- На 170HX `OPT_GEN23` (`0x82057c`) — pure OTP sense, неписабелен с любого уровня
  (rd=1) — **как наш `0x2157c`**. **Но Gen2 у них работает с фьюзом=1** → фьюз НЕ блокер.
- Реальный барьер — **priv-ring/PLM**. Записи политики (CYA_0/LINK_CONFIG_0/PRIV_MISC_1/
  `0x8872c`=6) срабатывают только **после открытия PLM** (`0x88fe8-ff0`, `0x8e1b0-bc` →
  `0xffffffff`); до того priv-ring дропает (`0xbadf5040`).
- Открытие PLM — **эксплойт SEC2 Booter**: GSP signature-buffer раздувают до `0xf800`,
  crafted Falcon ROP, переиспользуют подписанный Booter Load → arbitrary BAR0-write на
  Heavy-Secure. Баг = unbounded DMA в `booterVerifyLsSignatures_TU10X` (IMEM `0x29C4`).
  Суффикс `_TU10X` = **Turing**; гейт device `0x20C2`/`0x2082`.

[ВЫВОД] Примитив открытия PLM архитектурно привязан к **Turing+ (GSP + SEC2 Booter)**.
GP100 — **Pascal**: нет GSP, нет SEC2 Booter, нет `booterVerifyLsSignatures`. 50HX — TU116,
тоже Turing. Все CMP-прецеденты стоят на этом Turing+ SEC2-эксплойте → на Pascal не
портируется. GP100-замеры совпадают с 170HX-в-закрытом-виде: те же PLM priv-masked;
`CYA_0` bit2 уже=0; `0x8872c`=0.

## 4. [ВЫВОД] Итоговый статус Gen2 на CMP 100-100

- **Фьюз `0x2157c` — НЕ блокер** (170HX-прецедент: Gen2 с фьюзом=1). Формулировки
  «фьюз enforced» заменены на корректные во всех под-отчётах и памяти.
- **Барьер — priv-ring/PLM**, закрывающий регистры-рычаги (`0x8841c`/`0x8c040`/`0x8c2c0`/
  `0x8872c`/XP3G). На GP100 эти рычаги частью уже «открыты» (DIS_G2=0), но Phase-B записи
  не дают эффекта, потому что PLM (`0x88fe8-ff0`,`0x8e1b0-bc`) закрыты (priv-masked).
- **Открыть PLM на GP100 из доступного firmware нечем:** единственный известный примитив
  (SEC2 Booter эксплойт) — Turing+, у Pascal его нет; стоковый GP100 falcon эти PLM не
  трогает; host-запись priv-защищённых регистров дропается.
- **Gen2 x1 не достигнут.** Все host/ROM/reversible-пути исчерпаны измерением; карты целы.

## 5. [НЕИЗВЕСТНО] Что осталось и граница

- Теоретически остаётся **своя уязвимость в Pascal PMU/ACR** (arbitrary BAR0-write для
  открытия PLM). В разобранном firmware (RM580/CMP_FACTORY/MODS367) такого пути нет;
  среди CMP-карт Pascal-прецедентов нет.
- Это — **разработка эксплойта против code-signing/secure-boot (HS-falcon)**, отдельная
  тяжёлая offensive-RE задача. В этой сессии её вести не стали (граница: RE и
  документирование — да; построение рабочего обхода подписи секьюрной прошивки — нет).
- Также не разбирались: BootROM и ранний secure-boot до falcon, FWSEC/GFW, иные
  ucode-версии. «Не нашёл в разобранном коде» ≠ «нет нигде».
- Параллельная ветка (не про Gen2): **4-й HBM-стек** — FBIO-фьюз `0x21c14→0x21938`;
  flash = NO-GO (необратимо, nvflash не читает карту), live-poke не ретренит; flash-free
  тест = MODS floorsweep override (упирается в незавершённую v33 syscall-gate работу на lab).

## 6. Артефакты и воспроизведение (read-only)

Под-каталоги в `evidence_20261026_gen2x1/`: `native_speed_gate/`, `cap_register_readout/`,
`cap_write_probe/`, `gen3_stage_a/`, `fuse_override_probe/`, `plm_sweep/`, `gen2_170hx_compare/`.
Сырьё опытов на lab: `/REMOTE-USER/cmp100-lab/runs/…`.
ROM-дампы (uploads): CMP50 `ceccd66c-cmp50.Bin` sha `22814dfa…`;
CMP170 `89c4b295-268495_2.rom` sha `789e8f8d…`.

Исследование открытое: любой результат пересматривается новыми данными.
