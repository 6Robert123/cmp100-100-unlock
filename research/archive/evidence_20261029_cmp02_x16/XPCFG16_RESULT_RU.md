> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# CMP02: горячая проба поля 8c040 и восстановление

## Текущее состояние

Сервер восстановлен разрешённой оператором перезагрузкой. Boot ID теперь `UUID-REDACTED`. Все три прежних UUID доступны, каждая карта12GiB, used0, Gen1x1. Init, temperature monitor и display-manager active. CMP02: `8c040=08089000`, `88084=00453c11`, `88088=10110140`, `8c080=00000101`; исходные поля восстановлены. В новом boot-журнале нет Xid/AER/PCIe Bus Error/RmInitAdapter failed. SPI/ROM не записывались.

## Исполненная проба — не повторять без нового обоснования

По решению оператора продолжить поиск firmware-free x16 выполнена **одна экспериментальная** запись поля `[31:27]` в `BAR0+0x8c040` на прежней карте№3, UUID `GPU-REDACTED`, BDF02. Предварительное значение `0x08089000`; отправлено **0x80089000**, то есть поле1→16. Все младшие27бит, включая rate и нулевое low5, сохранены. Нулевое поле ширины не формировалось.

Это была гипотеза применения найденного поля после пайки, а не подтверждённый RAM-анлок. Сохранявшиеся disassembly связывают поле с шириной, но не устанавливают полный допустимый GP100 runtime-transition. Предварительный source review отметил отсутствие доказанного consumer/семантики поля и невозможность гарантировать BAR-откат при потере связи. Родитель всё же выбрал ограниченную ненулевую пробу при новом аппаратном вводе; проверка исхода опровергла её пригодность как рабочей процедуры.

После отправки слова endpoint config стал `ffffffff`, root LnkSta `0x1001` (width0, DLL inactive); NVIDIA записал Xid79 и154/reset required. Стабильный PCIe-доступ за5с не появился. **Readback нового значения регистра не получен**: нельзя утверждать, что16 было измеренно защёлкнуто. Экспериментальный root retrain не выполнялся, потому что его gate требовал доступного стабильного endpoint. BAR-откат после исчезновения endpoint не выполнялся; исход операции и cleanup были ошибочными.

Отдельно выполнен один восстановительный root Retrain Link при TLS1. Он не помог:20 чтений сохранили endpoint `ffffffff`, root width0. После этого с разрешения оператора выполнен `systemctl reboot`. Изменение boot ID и независимые postchecks подтвердили восстановление, а не просто отправку команды reboot.

## Доказательства и повторение

- Точный исполнявшийся исходник (archive reference: `xpcfg_width16_once.py`; original context may be excluded), SHA256 `192c5e953891545bbe0ebb5c2dd2072e1682b2f5630282c274f76074449c8ce3`.
- Runtime package (archive reference: `xpcfg-width16-run.tar`; original context may be excluded): source pin и дословный event journal; stdout (archive reference: `xpcfg-width16.stdout`; original context may be excluded), stderr (archive reference: `xpcfg-width16.stderr`; original context may be excluded).
- Журнал сбоя (archive reference: `xpcfg-width16-dmesg.failed.txt`; original context may be excluded), восстановительный retrain (archive reference: `xpcfg-recovery-root-retrain.jsonl`; original context may be excluded).
- Post-reboot health (archive reference: `reboot-final-health.txt`; original context may be excluded), регистры после reboot (archive reference: `width-registers-after-reboot.json`; original context may be excluded), новый kernel log (archive reference: `dmesg.after-reboot.txt`; original context may be excluded).
- Offline audit (archive reference: `audit_xpcfg_recovery.py`; original context may be excluded), результат (archive reference: `XPCFG16_RECOVERY_AUDIT.json`; original context may be excluded). Replay: `python3 evidence_20261029_cmp02_x16/audit_xpcfg_recovery.py`. PASS относится к проверке сохранённых доказательств; исход эксперимента — LINK_LOSS_NO_X16, recovery — REBOOT_RESTORED.

Исторический server width probe (archive reference: `prior_server_width/probe_20260830.txt`; original context may be excluded) отдельно проверял записи в8c080 и получил readback1/WRITABLE=NO. Он не был положительным контролем для8c040. Найденные V6/V8 изменяют ранний IFR в ROM. Публичные примеры [duggasco](https://github.com/duggasco/CMP100-210) и [Brazzo](https://github.com/Brazzo978/CmpUnlocker-100-210) относятся к **GV100/100-210** и также связывают x16 с SPI/IFR, а не доказывают volatile GP100-процедуру. Их нельзя выдавать за проверенный способ для15c2.

Новый результат не доказывает плохую пайку или невозможность x16. Он устанавливает, что **горячий одиночный переход8c040 high5=1→16 на этой карте не является рабочим RAM-путём**. Следующее исследование должно установить полный GP100 consumer/порядок ранней конфигурации PHY либо настоящий volatile override. Повторять эту запись, старую width0-пробу или прошивать V6/V8 автоматически нельзя.
