> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Кто владеет `0xebf500` (offline)

Проверка только pinned ELF MODS 520.112: `/LOCAL-USER/cmp100-100/evidence_20261022_photo_followup/mods520_112/mods.unpacked`, 255 426 008 байт, SHA-256 `b3df4ccda11851601ffb91bcf251f42db2f8e74df66a1dd497b83c527d5fd2ca`. Процесс, GPU, reset и runtime-список не запускались. Повтор: `topology_manager_registration_replay.py` (archive reference: `topology_manager_registration_replay.py`; original context may be excluded), квитанция `TOPOLOGY_MANAGER_REGISTRATION.json` (archive reference: `TOPOLOGY_MANAGER_REGISTRATION.json`; original context may be excluded), статус PASS.

## Результат

`0xebf500` не имеет ни одного прямого `CALL/JMP rel32`, `LEA` или `MOVABS`. Единственный абсолютный qword в файле лежит в vtable.

| Поле | Значение |
|---|---|
| Класс | `NvLinkDevIf::TopologyManagerProtobuf` |
| База | `NvLinkDevIf::TopologyManagerImpl` |
| Address point vtable | `0x4c692c8` |
| Слот метода | `+0x20` (`0x4c692e8` → `0xebf500`) |
| Тот же слот у базы | `0x30a8c60`, это другая функция |
| Конструктор | FDE `0xeb4ba0..0xeb4ca8`, запись vptr на `0xeb4c2c` |
| Фабрика | FDE `0xeb4cb0..0xeb4d90`, `operator new(0xC0)`, затем ctor |
| Статический инициализатор | FDE `0x4a9e50..0x4a9f47` |
| Слот `.init_array` | VA `0xf54a1b8`, индекс 698, указывает на `0x4a9e50` |
| Регистратор | `0xe65620` |

Инициализатор передаёт в `0xe65620` имя из rodata `TopologyManagerProtobuf`, порядок `0` и фабрику `0xeb4cb0`. Регистратор сохраняет указатель фабрики и вызывает лог с буквальной строкой `Registered NvLink topology manager implementation %s with order %u!!`. Корень таблицы, который он читает, — BSS `0xf8da3f0`: в файле этот образ нулевой, наполнение бывает только после старта процесса.

Это закрывает прежний пробел «кто регистрирует indirect `0xebf500`». Метод по-прежнему тот же producer, который грузит reset-global `0xf7a7fb8` и добавляет `NvLinkDevIf::SimGpuDev`. Владелец метода — менеджер NVLink-топологии, не показанный конструктор физического GP100.

## Что этим не доказано

Вызовов именно слота `+0x20` этого класса не отделено: общая форма `call [reg+0x20]` встречается тысячи раз и не выбирает этот тип. Откуда берётся входной vector в RSI, по-прежнему не видно. В просмотренных FDE нет сравнения `-only_pci_dev` и нет BDF `0000:02:00.0`. Соседние строки rodata про PCIe speed и NvSwitch не являются действиями этого метода. Физическая карта `10de:15c2` этой регистрацией с `TestDeviceMgr` не связана.

Следующий офлайн-шаг, если он нужен: найти вызов слота `+0x20` на объекте с vptr `0x4c692c8` и источник RSI-vector. Пока этого вызова нет, snapshot reset-list и повтор старых width-проб не становятся обоснованнее.
