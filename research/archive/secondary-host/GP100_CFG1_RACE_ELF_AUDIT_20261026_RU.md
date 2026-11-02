> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Static audit deployed `gp100_cfg1_race`: CFG1 stores, OPT_FBIO source, capacity scope

## Идентичность и метод

Проверен локальный Linux ELF `secondary-host/gp100_cfg1_race`, SHA-256 `a1ca427cb84f3225a5d337ebeaed20cdf1673d6ea7a5ee68a0dacf2bc8d88de6`, совпадающий с хэшем установленного helper, записанным в `STATUS-20261020.md:69–75` и подтверждённым текущей задачей. Это не `gp100_cfg1_race.legacy.c`: заметка прямо говорит, что старый hardcoded-FBPA исходник не использовался для deployment/compile. `audit_gp100_cfg1_race_elf.py` повторно проверяет pinned SHA и извлекает инструкционный raw через `objdump`; raw — `GP100_CFG1_RACE_ELF_RAW_20261026.txt`. ELF не запускался.

Стоковый init-скрипт в `cmp100-12gb-init.original` имеет SHA `2658def32d27d265f7cde38be91e1a37a283f4021e5fb642a9f0c4e792cffa9c` (равен указанному для stock helper script). Текущий адаптированный локальный `cmp100-12gb-init` отличается, SHA `5660314f89932ec8e564653c1bc49f46c961bfc70ff68c5a3e11e168953b9ee1`. Он задаёт `TARGET=0x02669000` (строка 5) и передаёт `BDF, TARGET` helper-у (строка 73); далее проверяет итоговую память как 12288 MiB (строки 88–96). Эти wrapper-строки объясняют назначение конкретного вызова, но само доказательство ниже извлечено из установленного ELF.

## Прослеженная BAR0 работа

Root дополнительно сверил read-only SSH26октября: установленный `/usr/local/sbin/cmp100-12gb-init` имеет именно SHA `2658def32d27d265f7cde38be91e1a37a283f4021e5fb642a9f0c4e792cffa9c`, то есть соответствует локальному `.original`, а не адаптированному wrapper. В нём также TARGET02669000 и вызов helper с BDF/TARGET. Установленный ELF helper имеет SHA `a1ca427cb84f3225a5d337ebeaed20cdf1673d6ea7a5ee68a0dacf2bc8d88de6`. Поэтому привязка store-audit к фактически установленным файлам подтверждена отдельно от исходного historical source. Root повторил reproducer: PASS.

1. ELF строит путь `/sys/bus/pci/devices/%s/resource0` (format string по ELF offset `0x2030`), открывает его с флагами `0x181002` (`open`, PC `0x1328`) и вызывает `mmap` длиной `0x1000000`, `PROT=3` (read/write), `MAP_SHARED=1`, offset `0` (PC `0x1370–0x138e`). Возвращённый mapping становится `RBX`; дальнейшие `[RBX+offset]` — offsets этого BAR0 mapping, а не прямые физические адреса.
2. `main` загружает dword `[RBX+0x21c14]` на PC `0x13e7` и повторно на `0x15a8`. В обоих местах это именно load в регистр (`R13D`/`R15D`). Код не содержит store в `[RBX+0x21c14]`.
3. `main` передаёт BAR base, считанное значение, CLI target и mode/counter в `plant_active` дважды (`0x15b8`, `0x15e0`). В helper `plant_active`:
   - копирует BAR base `RDI→RBX`, значение `ESI→R10D`, target `EDX→R11D` (`0x1941–0x1947`);
   - цикл `i=0..15` проверяет `BT R10D,i`; установленный бит перепрыгивает запись (`0x1958–0x195c`);
   - строит dword-адреса от `0x900204`, шаг `0x4000` (`0x194a`, `0x195e–0x196a`, `0x1992`), читает старое значение `[RBX + 0x900204 + i*0x4000]`, сравнивает его с `0x02559000` и CLI target (`0x196d–0x1981`), затем единственный BAR-подобный store — `mov [RDX],R11D` (`0x1983`). Следовательно, значение `0x21c14` используется как битовый selector пропуска при изменении ряда CFG1 dword-ов; оно не формируется и не записывается helper-ом.
4. Другие обращающиеся к BAR0 инструкции `main` в raw — чтения trigger/status/final CFG1 и диагностических полей. Writer-функция, получающая mapping base от `main`, — `plant_active`; её единственная запись через вычисленный BAR pointer показана выше. Обычные stack/argv/counter stores к BAR0 не относятся.

Итог по маске: в пределах полного disassembly именно этого pinned ELF helper **читает**, но не пишет OPT_FBIO source `[BAR0+0x21c14]`; не найдено ни записи этой ячейки, ни записи destination `[BAR0+0x21938]`. Это исключает cfg1 helper как producer/установщик этих двух FBIO-mask значений в рамках его собственного кода. Он использует mask-биты для выбора индексов CFG1, которые будет менять.

## Интерпретация ёмкости и граница

Helper предназначен для гонки/перезаписи CFG1 при инициализации: он ждёт наблюдаемое значение `0x02559000` (trigger в `main`, например `0x1478`) и пишет переданное target-значение; локальный wrapper передаёт `0x02669000`. Финальная проверка wrapper-а на 12288 MiB подтверждает лишь конфигурационную ёмкость, объявленную драйвером после такого CFG1 override. Это не измерение числа/плотности физических HBM die, не доказательство физического состава памяти и не источник исходной FBIO mask.

Это статический анализ ELF на точные open/mmap/dataflow/store пути. Он не доказывает аппаратную семантику каждого CFG1 поля сверх фактического software target, не связывает отдельный индекс `i` с физической FBPA без независимой таблицы адресации и не является runtime readback этого ELF. Новых GPU/PCI/MMIO действий не выполнялось.

## Воспроизведение

Из workspace выполнить `python3 secondary-host/audit_gp100_cfg1_race_elf.py`. Ожидаемый вывод включает ELF pinned SHA, путь raw и две ссылки `0x21c14`. Аудитор завершится ошибкой при несовпадении ELF SHA или изменении ключевых operand-проверок.
