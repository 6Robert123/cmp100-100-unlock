> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Проверка качества перед продолжением x16

Каталог всех исследований — `ARCHIVE_PREP_20261031`. Эта папка `evidence_20261031_archive_reassessment` — более позднее офлайн-дополнение и в замороженный inventory/path map не входит. `ARCHIVE_PREP` не изменялся. GPU, SSH, reset, SPI и MODS не запускались.

## Что сверено

- Манифест этой папки до правки: 44 файла, размеры и SHA совпали, лишних файлов не было.
- Потоковый SHA pinned ELF совпал с replay-квитанциями: `b3df4ccda11851601ffb91bcf251f42db2f8e74df66a1dd497b83c527d5fd2ca`, размер 255 426 008. В `pcie_trace/RESULT_RU.md` был один неверный полубайт `f8f74d`. Исправлено.
- Прямой rel32-поиск `0xebf500` повторно дал ноль. Это не ошибка прошлого отчёта.
- Четыре bridge-снимка в parent-аудите действительно не содержат 8 байт reset-global `0xf7a7fb8`. Отдельно найдены ещё пять `cpu_snapshot*.json` от 30 октября. Ни в одном нет адреса `f7a7fb8` / `259686328`. Четыре упоминают `global_manager`, то есть `GpuDevMgr`, и не заменяют reset-list.
- В coverage ledger архива x16-материалы размечены честно: семантически прочитанные отчёты отделены от inventory-only сырых JSON/TXT. Единственный x16 `RESULT*.md` вне `full_semantic_read` — superseded `evidence_20261029_cmp02_x16/mods_width_path/RESULT_RU.md`. Заново не прочитаны сотни inventory-only файлов; архив сам не называет их смысловым разбором.
- HBM-арифметика 4-Hi/8-Hi в parent JSON внутренне согласована и не является измерением платы. SPI join по-прежнему отделяет образы 26 октября от software-read текущей CMP02. Частично прочитанные длинные вложения в triage так и помечены; целиком они в этот проход не перечитывались.

## Что упущено и закрыто сейчас

Прошлая трасса оставила владельца indirect `0xebf500` открытым. Повторный поиск указателя нашёл один qword: слот `+0x20` vtable `NvLinkDevIf::TopologyManagerProtobuf`, регистрация из `.init_array[698]`. Подробности: [`pcie_trace/TOPOLOGY_MANAGER_REGISTRATION_RU.md`](pcie_trace/TOPOLOGY_MANAGER_REGISTRATION_RU.md).

Входной vector, вызов этого слота на живом объекте и связь с физическим GP100 остаются открытыми. Нового способа получить x16 нет.
