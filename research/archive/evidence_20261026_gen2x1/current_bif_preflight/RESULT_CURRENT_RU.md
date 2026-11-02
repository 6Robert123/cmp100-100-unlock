> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Свежий BIF preflight CMP03, 26 октября

Исполнен отдельный kcore-only reader с pin d40c7eb31d2086e34f2993cd00f2357e6fc8a72a829587a7d69d0a41b77c663f на проверенном boot UUID-REDACTED. SSH/reader exit0, stderr пуст. Двойной снимок равен: BIF +954/+955=0/0, +a34/+a38=0/0, +9a8/+9ac=0/0; GPU +5420=0. Identity/health до/после совпали: все три GPU доступны, память 12/16/12GiB, used0, links1x1/3x16/1x1.

Это опровергает перенос исторического +954=1 на текущее состояние. Штатный RegistryDwords пуст; прежний global5 опыт не воспроизведён. Следующий различитель — штатная временная policy RMPcieLinkSpeed=5 с фиксацией BIF и обязательным восстановлением профиля, прежде чем решать о SET2. Kernel data читаются динамически через проверенные pointers; прямого BAR/MMIO в reader нет. Повторная сверка pointers сужает гонки, но не является RM lifetime lock.

Запуск: `ssh REMOTE-HOST 'sudo -n timeout --signal=TERM --kill-after=2s 25s python3 -B /var/tmp/cmp03-bif-preflight-20261026-kcore-only.py'`. Raw JSON: current.stdout.json (archive reference: `current.stdout.json`; original context may be excluded); локальная проверка результата: CURRENT_AUDIT.json (archive reference: `CURRENT_AUDIT.json`; original context may be excluded). Gen2 не достигнут; SET, root TLS и driver profile в этом снимке не менялись.
