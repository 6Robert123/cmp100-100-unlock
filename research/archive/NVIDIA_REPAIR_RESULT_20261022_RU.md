> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Ремонт NVIDIA/NVML на lab — 2026-10-22

- Причина: загруженный и HWE-latelink модуль NVIDIA был `580.173.02`, userspace NVML — `580.178.04`.
- Установлены только `dkms`, `nvidia-kernel-source-580=580.178.04-0ubuntu0.24.04.1`, `nvidia-dkms-580=580.178.04-0ubuntu0.24.04.1`: 3 новых пакета, без обновлений и удалений.
- DKMS успешно собрал модули для 7.0.0-30 и 7.0.0-38. Штатная сохранённая загрузка оставлена на `7.0.0-30-generic`.
- После единственной перезагрузки boot ID: `UUID-REDACTED`; реально загружен модуль `580.178.04` из `/lib/modules/7.0.0-30-generic/updates/dkms/nvidia.ko.zst`. NVML совпадает и `nvidia-smi` работает.
- GPU: CMP01 `12288 MiB`, P100 `16384 MiB`, CMP03 `12288 MiB`; ожидаемые UUID сохранены. `cmp100-12gb.service` завершён успешно (`2 / 2`), монитор температуры активен. Xid нет.
- Температуры после загрузки и восстановления вентиляторов: CMP01 `45°C`, P100 `47°C`, CMP03 `42°C`, загрузка 0%.
- Fintek `f71889a` снова определён штатным `f71882fg`. До изменения: fan1 `255/manual, 1992 rpm`, fan2 `166/auto, 1573 rpm`, fan3 `166/auto, 946 rpm`. Через 10 с все три канала `255/manual`: `1981 / 2139 / 1180 rpm`.
- Custom CMP scripts, units и blacklist не изменены. Постоянная автозагрузка полного режима вентиляторов не добавлялась.

Исходные резервные копии и пакетный снимок находятся в `nvidia_repair_20261022_baseline/`.
