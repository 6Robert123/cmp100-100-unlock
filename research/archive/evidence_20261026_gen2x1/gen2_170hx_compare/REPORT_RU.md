> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# CMP100-100 ↔ P100 ↔ 170HX: сравнение регистров Gen2-пути (read-only)

Дата: 2026-10-27. boot_id осн. lab `UUID-REDACTED`.
Всё read-only: два независимых пути (BAR0 `resource0` mmap + config-space `setpci`,
совпали один-в-один для LnkCap/LnkCap2/LnkSta/LnkCtl2). Записи/unbind/reset/fuse-clock
НЕ трогались. 170HX-данные — [ИЗ АРХИВА] `cmp100-research/sources/cmp170hx/docs/unlock/pcie-gen2.md`
и `.../hardware/fuses-and-otp.md`.

## [ИЗМЕРЕНО] Таблица (осн. lab)

| рег | CMP01 | CMP03 | P100 | статус |
|---|---|---|---|---|
| `0x2157c`/`0x21580`/`0x21250` | 1/1/1 | 1/1/1 | 0/0/0 | CMP≠P100 (коррелируют с Gen1) |
| `0x88ab4` PLM | `0x8f` | `0x8f` | `0x8f` | одинаково (не различитель) |
| `0x88084` LnkCap | `00453c11` | `00453c11` | `00454103` | CMP≠P100 |
| `0x880a4` LnkCap2 | `0x02` | `0x02` | `0x0e` | CMP≠P100 |
| `0x880a8` LnkCtl2 | 0 | 0 | `1e0003` | CMP≠P100 |
| `0x88088` LnkSta | Gen1x1 | Gen1x1 | Gen3x16 | CMP≠P100 |
| `0x8841c` PRIV_MISC_1 | `c0b46500` | `c0b46500` | `00b40500` | CMP≠P100 |
| `0x88610`/`0x8860c` | 0/`0800` | 0/`0800` | `1001`/`1801` | CMP≠P100 |
| `0x8c2c0` CYA_0 | `068711b3` | `068711b3` | `060711b2` | **bit2(DIS_G2)=0 у ВСЕХ** |
| `0x8c040` LINK_CONFIG_0 | `08089000` | `08089000` | `80009000` | CMP≠P100 ([19:18] 2↔0) |
| `0x8c1c0` PL_LINK_RATE | `00210036` | `00210036` | `00030036` | CMP≠P100 |
| `0x8872c` XVE_OVR | 0 | 0 | 0 | одинаково |
| `0x21000` FUSECTRL-канд. | `c0040000` | `c0040000` | `c0040000` | одинаково |
| `0x21040` EN_SW_OVR-канд. | 0 | 0 | 0 | одинаково |
| **PLM: `0x88fe8/fec/ff0/ff4/ff8`** | BADF | BADF | BADF | **priv-masked на всех** |
| **XP3G_PLM `0x8e1b0/b4/b8/bc`** | BADF | BADF | BADF | **priv-masked на всех** |
| `0x21078/7c/80/84` | BADF | BADF | BADF | priv-masked |

BADF = `0xbadf5040` priv-masked sentinel (не значение).

## [ИЗ АРХИВА] Механизм 170HX Gen2 (docs/unlock/pcie-gen2.md)

Три фазы:
- **Phase A — 25 Booter-routed записей** (SEC2 Booter privilege, crafted Falcon signature =
  arbitrary BAR0-write primitive): открывает PLM `XP3G_PLM 0x8e1b0-bc`, `XVE_D0/D4/D8
  0x88fe8/fec/ff0`, `OPTB 0x8200d0-f4` (10), `FEAT_OVR_ECC_PLM 0x823800` = `0xffffffff`;
  плюс XP3G value/enable (`0x8e110-12c`) и `PRIV_MISC_1 0x8841c`.
- **Phase B — 6 plain BAR0 записей** (срабатывают ТОЛЬКО после открытия PLM; до этого
  priv-ring дропает и возвращает `0xbadf5040`): `VSEC_HIERARCHY 0x88610`, `LINK_CTRL_2 0x880a8`
  (target=2), `CYA_0 0x8c2c0` (clear bit2 DIS_G2), `LINK_CONFIG_0 0x8c040` (MAX_RATE [19:18]=2),
  `PL_LINK_RATE 0x8c1c0` (не обязательно), `XVE_OVR 0x8872c=6`.
- **Phase C — retrain с root-порта.**

Критично [ИЗ АРХИВА, дословно]: `OPT_GEN23` (`0x8205 7c`) — «pure OTP fuse-sense reflection
with no write port; every attempt from every privilege level returns status=0xffff rd=1».
**И Gen2 работает всё равно** — фьюз обходят через PLM-gated overrides. То есть неписабельность
OPT_GEN23 — НЕ блокер.

## [ВЫВОД] Что это значит для GP100 (без «фьюз enforced»)

1. **OPT_GEN23 на GP100 (`0x2157c`) неписабелен — как на 170HX, и это НЕ блокер Gen2.**
   Прежняя формулировка «фьюз держит» уточняется: 170HX-прецедент показывает, что Gen2
   достижим с OPT_GEN23=1, если открыты PLM.
2. **GP100 имеет те же PLM/override-регистры** (`0x88fe8-ff0`, `0x8e1b0-bc`): они читаются
   `0xbadf5040` = priv-masked sentinel — ровно как 170HX в ЗАКРЫТОМ состоянии. Значит они
   ЕСТЬ и priv-защищены (не «отсутствуют на Pascal», как я раньше предполагал про XP3G).
3. **`CYA_0` bit2 (DIS_G2) у GP100 уже = 0** на обеих картах — один из Phase-B рычагов уже
   «открыт». `0x8872c` читается 0 (как 170HX stock до записи).
4. **Настоящая разница механизма:** 170HX открывает PLM через **SEC2 Booter** (signature-exploit
   privilege escalation). **GP100 (Pascal) не имеет SEC2/Booter.** Его secure-движок — PMU/ACR
   falcon (эта сессия: gm200/v1, доказанного custom-payload нет). Наши Stage A/B (clear CYA,
   retrain) не сработали именно потому, что **PLM не открыты** — plain-BAR0 overrides priv-ring
   дропает, как и на 170HX до Phase A.

**Точно локализованная цель Gen2 на GP100:** не «снять фьюз», а **найти/построить в GP100
secure-falcon (PMU/ACR) privilege-escalation BAR0-write primitive, чтобы открыть PLM**
(`0x88fe8-ff0`, `0x8e1b0-bc` → `0xffffffff`), затем повторить Phase B (CYA_0/LINK_CONFIG_0/
PRIV_MISC_1/XVE_OVR) + Phase C retrain. Это аналог SEC2-Booter-эксплойта 170HX на Pascal-ACR.
Достижимость [НЕИЗВЕСТНО]; но цель теперь в правильном месте (priv-ring/PLM + secure-движок),
а не в неписабельном фьюзе.

## ROM-дампы (залогированы, для разбора 170HX/50HX путей)
- CMP50 `ceccd66c-cmp50.Bin`: 8 МиБ, sha `22814dfadc13d9fa1e778d333147957c8037927fcb89a8abd02a48f3df6b9e27`.
- CMP170 `89c4b295-268495_2.rom`: 1020 КиБ, sha `789e8f8d4c70dc9eb114c9fdcef6ed2d00aedf15a1eca522519de02042818723`.

## [ИЗ АРХИВА + ИЗМЕРЕНО] Дополнение 2026-10-27: примитив открытия PLM архитектурно-привязан

**П1 (read-only скан GP100 vs P100):** блок XVE-PLM `0x88fXX` (вкл. 170HX `0x88fe8/fec/ff0`)
целиком priv-masked (BADF) на ОБЕИХ картах — ни одного открытого PLM. XP3G override-слоты
`0x8e110-12c` читаются (=0), но их PLM `0x8e1b0-bc` priv-masked. Открытого PLM-эквивалента
на GP100 нет.

**П2 (170HX how-it-works.md):** открытие PLM у 170HX — это ЭКСПЛОЙТ SEC2 Booter:
драйвер раздувает GSP signature-buffer до `0xf800`, кладёт crafted Falcon ROP-payload,
переиспользует подписанный SEC2 Booter Load → arbitrary BAR0-write на Heavy-Secure. Баг —
unbounded DMA в `booterVerifyLsSignatures_TU10X` (IMEM `0x29C4`, длина из `sizeOfSignature`
без bounds-check, dst DMEM `0x0800`). Гейт device ID `0x20C2`/`0x2082`. Суффикс `_TU10X` = Turing.

**GP100 falcon-скан:** `CMP_FACTORY`/`P100` ucode эти PLM-адреса (`0x88fe8-ff0`,`0x8e1b0-bc`)
не трогают (в разобранном коде).

[ВЫВОД] Примитив, открывающий PLM на 170HX, **архитектурно привязан к Turing+ (GSP + SEC2
Booter)**. GP100 — Pascal: нет GSP, нет SEC2 Booter, нет `booterVerifyLsSignatures_TU10X`.
50HX (TU116) — тоже Turing. Т.е. ВСЕ CMP-прецеденты разблокировки стоят на Turing+ SEC2-
эксплойте, которого на Pascal архитектурно нет. Barrier Gen2 на GP100 (priv-ring/PLM)
реален, но путь 170HX к нему не портируется.

Оставшаяся теоретическая возможность [НЕИЗВЕСТНО, без прецедента]: своя уязвимость в
Pascal PMU/ACR, дающая arbitrary BAR0-write для открытия PLM. В разобранном GP100-firmware
(RM580/CMP_FACTORY/MODS367) такого пути не найдено; среди CMP-прецедентов Pascal-примеров нет.
Это отдельная тяжёлая RE-задача (поиск 0-day в Pascal-ACR), не продолжение текущего пути.
