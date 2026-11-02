> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Первичные сведения об IFR и фазе PCIe

[Патент NVIDIA US8839039B2](https://patents.google.com/patent/US8839039B2/en), описание FIG.2–5: IFR после reset читает ROM и выполняет register operations через внутренний интерфейс. Link-training holdoff может освобождаться самим IFR либо передаваться микроконтроллеру. Описаны RMW, repeated writes и проверки данных; конкретного GP100 формата0x82, MMIO-адресов или рабочего host replay API патент не задаёт. Номера236/237 — обозначения элементов рисунка, **не offsets для записи**.

Это объясняет класс раннего механизма, но не доказывает его точное воплощение на CMP02. Подстановка этого описания вместо GP100 consumer была бы новым предположением.

[Документация Linux nova-core](https://docs.kernel.org/next/gpu/nova/core/vbios.html), раздел IFR Header: NVGI предшествует PCI ROM; FIXED1 содержит SW version и offset extended section, способы поиска PCI image зависят от версии. Это документация парсинга образа для драйвера, не исполнителя IFR-инструкций и не включенияx16 на GP100.

Проверенный новый source-вопрос: в MODS367 есть сообщения о PMU IFR `started/inited`. Нужно найти их настоящий consumer и выяснить, является ли он только проверкой состояния или содержит управляемый replay/handoff. До такой связи source не задаёт новой аппаратной команды. Никаких payload/ROM images или GPU writes не создавалось.
