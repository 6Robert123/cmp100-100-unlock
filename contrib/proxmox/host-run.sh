#!/usr/bin/env bash
# On the Proxmox HOST, as root. Manually arm the host-side helper for a card that is passed
# through to an already running VM (the hookscript does the same automatically).
#   [QUIET=10] [PERIOD_US=200] ./host-run.sh 0000:XX:00.0 [TIMEOUT_S=120] [VMID]
# QUIET: for the first N seconds the helper does NOT touch BAR0 (OVMF reset phase).
# It writes to the card's BAR0 only at the moment of the trigger (the same CFG1 writes as
# the in-guest helper). Log: /root/cmp100-host-<time>/helper.log
set -euo pipefail
BDF=${1:?usage: $0 BDF [TIMEOUT_S] [VMID]}; T=${2:-120}; FORCE_VMID=${3:-}
case "$BDF" in *:*:*.*) ;; *) BDF="0000:$BDF" ;; esac
HERE=$(dirname "$(readlink -f "$0")"); BIN=${BIN:-$HERE/gp100_cfg1_race_host}
D=/sys/bus/pci/devices/$BDF

[ "$(id -u)" = 0 ] || { echo "must be root"; exit 1; }
[ -x "$BIN" ] || { echo "no helper binary: $BIN (build it, see README.md)"; exit 1; }
[ -e "$D" ] || { echo "no device $BDF"; exit 1; }
[ "$(cat "$D/vendor")/$(cat "$D/device")" = 0x10de/0x15c2 ] || { echo "$BDF is not 10de:15c2"; exit 1; }

echo "== preflight (read-only)"
echo "  driver: $(basename "$(readlink "$D/driver" 2>/dev/null)" 2>/dev/null || echo none)"
echo "  helper sha256: $(sha256sum "$BIN" | cut -d' ' -f1)"
echo "  kernel: $(uname -r); $(grep -h IO_STRICT_DEVMEM /boot/config-"$(uname -r)" 2>/dev/null || echo 'IO_STRICT_DEVMEM: config not found')"
grep -qw 'iomem=relaxed' /proc/cmdline && echo "  cmdline: iomem=relaxed present" || echo "  cmdline: iomem=relaxed absent"
# VMs holding the card: a hostpci entry with this slot (with/without domain/function) or a PCI mapping.
SHORT=${BDF#0000:}; SLOT=${SHORT%.*}
MAPCFG=${MAPCFG:-/etc/pve/mapping/pci.cfg}; QSDIR=${QSDIR:-/etc/pve/qemu-server}
MAPS=""
[ -r "$MAPCFG" ] && MAPS=$(awk -v s="$SLOT" '/^[^ \t#]/{name=$1} index($0,"path=0000:"s)||index($0,"path="s){print name}' "$MAPCFG" | sort -u || true)
vms_with_card() {
  local c id m
  for c in "$QSDIR"/*.conf; do
    [ -r "$c" ] || continue
    id=$(basename "$c" .conf)
    # main section only (before the first [snapshot]); no grep -q: with pipefail an early grep exit gives SIGPIPE
    main=$(awk '/^\[/{exit} {print}' "$c")
    if printf '%s\n' "$main" | grep -E "^hostpci[0-9]+: *(host=)?(0000:)?${SLOT}(\.0)?([,[:space:]]|$)" >/dev/null; then echo "$id"; continue; fi
    for m in $MAPS; do
      if printf '%s\n' "$main" | grep -E "^hostpci[0-9]+:.*mapping=${m}([,[:space:]]|$)" >/dev/null; then echo "$id"; break; fi
    done
  done
  return 0
}
if [ -n "$MAPS" ]; then echo "  PCI mapping with this card: $MAPS"; fi
for id in $(vms_with_card); do echo "  VM $id: $(qm status "$id" | awk '{print $2}')"; done

RUNNING=0
if [ -n "$FORCE_VMID" ]; then
  [ "$(qm status "$FORCE_VMID" | awk '{print $2}')" = running ] && RUNNING=1
  echo "  VMID given explicitly: $FORCE_VMID ($(qm status "$FORCE_VMID" | awk '{print $2}'))"
else
  for id in $(vms_with_card); do [ "$(qm status "$id" | awk '{print $2}')" = running ] && RUNNING=1; done
fi
# Arming before qm start (polling BAR0 across the vfio reset) hung a host with an NMI: forbidden.
[ $RUNNING = 1 ] || { echo "REFUSED: no running VM holds this card. qm start first, then the helper."; exit 1; }

L=/root/cmp100-host-$(date +%Y%m%d-%H%M%S); mkdir -p "$L"
echo "== helper v4 (timeout ${T}s, quiet ${QUIET:-10}s, period ${PERIOD_US:-200}us, gate), log $L/helper.log"
echo "   After ARMED, load the driver in the guest. Do not reboot/reset the VM while the helper runs."
set +e
"$BIN" "$BDF" 0x02669000 "$T" --wait-reset --quiet "${QUIET:-10}" --period-us "${PERIOD_US:-200}" 2>&1 | tee "$L/helper.log"
RC=${PIPESTATUS[0]}
set -e
echo "helper rc=$RC (0 = HIT, 3 = TIMEOUT, 2 = cannot open/map BAR0, 4 = card not responding or gate closed at arming - refused)"
