# Proxmox VE / vfio: host-side helper

Brings a **GP100 CMP 100-100** (`10de:15c2`) that is passed through to a VM up with 12 GB
without installing anything in the guest (used with Linux and Windows guests).
Background, register tables before/after driver init and raw logs: issue #2.

`gp100_cfg1_race_host.c` is a host-side variant (v4) of
`unlock/historical/gp100_cfg1_race_optfbio.c`. The register logic (addresses, trigger, target,
LMR window) is unchanged; only host-safety changes were made (see the file header).

## WARNING: this can hang a host

On an HP Gen8 host, polling BAR0 across the vfio reset at VM start raised an **NMI and
hung the host**. Rules this kit follows, and which you must keep if you adapt it:

- never run the helper before the VM is running (`pre-start`/`post-stop` kill it);
- `--quiet S` keeps BAR0 untouched while the guest firmware resets the card;
- the helper checks a reset gate (card COMMAND, upstream Secondary Bus Reset, upstream DLLLA)
  before every BAR0 access and polls every `--period-us` instead of a tight loop;
- do not reboot the guest from inside while armed. An in-guest reboot resets the card and it
  returns with 6 GB. Use `qm stop` / `qm start`.

Not verified here: other host platforms; Windows guests under a memory test (only the Linux
guest was memtested: `PASS_FULL`, no AER). Reports are welcome.

## Build

The binary is not shipped; build and pin your own:

```sh
gcc -O2 -Wall -Wextra -o gp100_cfg1_race_host gp100_cfg1_race_host.c
sha256sum gp100_cfg1_race_host
```

## Automatic: hookscript

```sh
install -D -m 0755 gp100_cfg1_race_host /usr/local/lib/cmp100/gp100_cfg1_race_host
install -m 0755 cmp100-hook.sh /var/lib/vz/snippets/cmp100-hook.sh
cat > /etc/default/cmp100 <<EOF
BDF=0000:XX:00.0
HELPER_SHA=$(sha256sum /usr/local/lib/cmp100/gp100_cfg1_race_host | cut -d' ' -f1)
EOF
qm set <vmid> --hookscript local:snippets/cmp100-hook.sh
```

In `post-start` the hook starts the helper as a transient systemd unit
(`journalctl -u cmp100-helper-<vmid>`); it always exits 0 and never blocks `qm start`.
Tunables in `/etc/default/cmp100`: `QUIET` (10), `TIMEOUT` (120), `PERIOD_US` (200), `TARGET`.

## Manual: `host-run.sh`

With the VM already running: `./host-run.sh 0000:XX:00.0 [TIMEOUT_S] [VMID]` (root). It prints
a read-only preflight, refuses if no running VM holds the card, and logs to
`/root/cmp100-host-<time>/helper.log`. Exit codes: 0 HIT, 3 timeout, 2 cannot map BAR0,
4 card not responding / gate closed at arming. The kernel may need `iomem=relaxed` for BAR0
access from userspace (the preflight reports it).

After the helper reports HIT, load the NVIDIA driver in the guest and verify the memory range
as described in `CONTRIBUTING.md`; `nvidia-smi` capacity alone is not sufficient.
