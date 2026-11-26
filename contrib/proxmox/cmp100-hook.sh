#!/usr/bin/env bash
# Proxmox VE hookscript: arm the host-side CFG1 helper after the VM has started,
# so a passthrough CMP 100-100 comes up with 12 GB (no software inside the guest).
#
# Install:
#   install -D -m 0755 gp100_cfg1_race_host /usr/local/lib/cmp100/gp100_cfg1_race_host
#   install -m 0755 cmp100-hook.sh /var/lib/vz/snippets/cmp100-hook.sh
#   printf 'BDF=0000:XX:00.0\nHELPER_SHA=%s\n' "$(sha256sum /usr/local/lib/cmp100/gp100_cfg1_race_host | cut -d' ' -f1)" > /etc/default/cmp100
#   qm set <vmid> --hookscript local:snippets/cmp100-hook.sh
# PVE calls: cmp100-hook.sh <vmid> <phase>
#
# Safety rules (a host hung with an NMI when BAR0 was polled across the vfio reset at VM start):
#  - the helper NEVER runs while the VM is starting: in pre-start/pre-stop/post-stop any
#    instance for this VM is stopped before QEMU/vfio resets the card;
#  - it is started only in post-start (start-time resets are done) and stays quiet for $QUIET s;
#  - the hook always exits 0 so it can never block a VM start.
# A guest reboot from INSIDE the guest does not call the hook; the card resets and comes back
# with 6 GB. Restart with qm stop / qm start instead.

CONF=${CMP100_CONF:-/etc/default/cmp100}
[ -r "$CONF" ] && . "$CONF"

BDF=${BDF:?set BDF in $CONF}
HELPER=${HELPER:-/usr/local/lib/cmp100/gp100_cfg1_race_host}
HELPER_SHA=${HELPER_SHA:-}      # sha256 of YOUR build of the helper; required
QUIET=${QUIET:-10}              # s; no BAR0 access while the guest firmware (OVMF) resets the card
TIMEOUT=${TIMEOUT:-120}         # s; keep it short: a long window can catch an in-guest reboot
TARGET=${TARGET:-0x02669000}
PERIOD_US=${PERIOD_US:-200}

vmid=$1; phase=$2
unit=cmp100-helper-$vmid
log() { logger -t cmp100-hook "VM $vmid $phase: $*"; }

stop_helper() {
  if systemctl is-active -q "$unit" 2>/dev/null; then
    systemctl stop "$unit" 2>/dev/null; log "helper stopped"
  fi
  systemctl reset-failed "$unit" 2>/dev/null
}

case "$phase" in
pre-start|post-stop|pre-stop)
  stop_helper ;;
post-start)
  stop_helper
  if [ -z "$HELPER_SHA" ] || [ ! -x "$HELPER" ] \
     || [ "$(sha256sum "$HELPER" | cut -d' ' -f1)" != "$HELPER_SHA" ]; then
    log "helper missing or SHA256 mismatch (set HELPER_SHA in $CONF) - not starting"; exit 0
  fi
  D=/sys/bus/pci/devices/$BDF
  if [ "$(cat "$D/vendor" 2>/dev/null)/$(cat "$D/device" 2>/dev/null)" != 0x10de/0x15c2 ] \
     || [ "$(basename "$(readlink "$D/driver" 2>/dev/null)" 2>/dev/null)" != vfio-pci ]; then
    log "$BDF is not a CMP 10de:15c2 bound to vfio-pci - not starting"; exit 0
  fi
  # Separate transient unit: does not block qm start; log: journalctl -u $unit
  systemd-run --unit="$unit" --collect --quiet \
    "$HELPER" "$BDF" "$TARGET" "$TIMEOUT" --wait-reset --quiet "$QUIET" --period-us "$PERIOD_US" \
    && log "helper started (quiet ${QUIET}s, timeout ${TIMEOUT}s)" \
    || log "failed to start helper"
  ;;
esac
exit 0
