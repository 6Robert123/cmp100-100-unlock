# Multi-CMP initializer and boot entry

Project name: **CMP 100-100 UNLOCK**. Author: **6Robert123**.

One `.sh` contains a Python 3.8+ stdlib controller and the exact pinned historical helper ELF. Linux x86_64 and glibc >=2.38 are required (the archived helper was used on Ubuntu 24.04). A [two-CMP hot apply trial](../validation/hardware_20261102/README.md) passed; mixed NVIDIA hosts and cold boot remain hardware-unvalidated.

## Modes

```sh
bash cmp100-12gb.sh --help
sudo bash cmp100-12gb.sh --check
sudo bash cmp100-12gb.sh --apply
sudo bash cmp100-12gb.sh --apply 0000:01:00.0 0000:03:00.0
sudo bash cmp100-12gb.sh --install-boot
sudo bash cmp100-12gb.sh --uninstall-boot
```

- Default/`--check`: read-only inventory, PCI identity/topology/ownership/prerequisite and embedded-hash checks; no SMI, module load, helper execution or sysfs write.
- `--apply`: NVIDIA core must already be loaded. All CMP `10de:15c2` are selected by default; BDF arguments may select a subset. Other adapters are allowed.
- `--boot`: permits loading the NVIDIA core once if absent, then uses the same per-device sequence. Intended for the managed boot entry, not a separate global unload/reload path.
- `--install-boot`: copies this script and a managed systemd unit; enables the entry for a subsequent boot. It never uses `start`/`--now`, changes module blacklists, stops services, installs packages, or reboots. An enabled legacy `cmp100-12gb.service` must be resolved manually first; the installer refuses competing startup. Unmanaged/symlinked destination files are refused.
- `--uninstall-boot`: disables/removes only this managed entry; it does not stop an active GPU session. A running initializer blocks installation changes. Installer failure attempts to restore both previous file bytes and enable-state and reports rollback errors.

## Per-device sequence

1. Preflight **all selected CMPs**; lock the initializer, verify execution in a private temporary directory, and extract/hash-check the helper. `/run`, `/var/tmp`, then `/tmp` are tried; no mount options are changed. Failure stops before GPU changes. Remember non-target driver bindings.
2. Leave the common NVIDIA core loaded. For only the current CMP, set a temporary matching hold, choose the checked isolated reset method, unbind, reset once, and enable PCI memory decode with a masked COMMAND write/readback.
3. Start the helper and require `ARMED` before allowing/binding the target to NVIDIA.
4. Use target-filtered `nvidia-smi --id=BDF` while the helper is active, or accept a successful `HIT_NS` interception already completed during bind. Reap the helper and validate the target.
5. Repeat for the next CMP without unloading modules. Recheck every selected CMP at 12288 MiB in the final pass; restore settings and check bindings before `READY`.

P100/other NVIDIA/AMD devices never receive reset/helper/unbind/config/override/bind writes from the initializer. Their presence alone is allowed. On cold module registration, previously unbound NVIDIA devices may attach normally; already bound non-targets must keep their bindings. This is not a promise that loading a driver has no system-wide registration effects.

Selected GPUs must be idle. Device minor numbers are read from NVIDIA's per-BDF proc metadata rather than inferred from enumeration order. Shared NVIDIA control/UVM clients that cannot be attributed safely cause refusal; this version does not guarantee hot operation alongside arbitrary active other-GPU workloads. Nouveau/MODS ownership, boot VGA, custom overrides and shared reset bridges cause refusal. Adequate cooling and a planned idle initialization window are prerequisites.

## Autostart

The installed entry is `/etc/systemd/system/cmp100-12gb-unlock.service`; the self-contained script is `/usr/local/sbin/cmp100-12gb-unlock`. It runs `--boot` before the display manager, persistence service and multi-user target. It does not copy the old host-specific fan service or blacklist. Workload units that can start earlier must be deliberately ordered after this initializer. An already active GPU client causes a clean refusal rather than being killed.

Installing the entry does not run it. Do not enable a second initializer while the historical one is enabled. Current boot state is not changed by this offline preparation.

## Failure and evidence

Commands/helper windows are bounded. Failure stops/reaps the current helper, restores touched reset-method/override settings, reports partial state and exits nonzero. Before any reset, a lost original binding may be restored; after a reset it does not probe without a helper or perform a reset retry/reboot. A later-card failure does not erase the earlier cards' observed result and is never reported as all-card success. Preserve the private log directory printed under `/var/log/cmp100-12gb-*` and inspect state before recovery.

The historical deployed ELF SHA is `a1ca427cb84f3225a5d337ebeaed20cdf1673d6ea7a5ee68a0dacf2bc8d88de6`. Its exact matching source/build recipe is still missing. A supplied [VRAM log](../validation/vram_alias_20261102/REVIEW_RU.md) now records two successful 11264-MiB full-mode runs. That supplied memory log does not test this initializer or all 12288 MiB; the separate new trial tests initialization/capacity only. NVIDIA-reported 12288 MiB is a capacity observation, not proof of all-address stability, physical HBM density or a fourth stack. Gen2/x16 are outside this initializer.

The server's historical multi-card boot worked using a global unload/reload loop with ignored errors. The later archived V3 alternative failed at its first core load; it does not validate this candidate. [Method comparison](../docs/MULTICARD_METHOD_REVIEW.md).

## Offline tests

```sh
python3 -B validation/test_multicard_engine.py
python3 -B validation/test_release_guards.py
python3 -B tools/check_bundle.py
```

Fixture tests call the actual embedded controller with an in-memory backend and exercise installer file changes in temporary directories with a fake systemctl. They do not execute the ELF or access GPUs. The original implementation checks were CPU-only; the later two-CMP hardware trial is documented separately above. No boot entry was installed in that trial.

The runtime identity contract is current PCI vendor/device plus selected BDF and capacity; it does not pin a particular board UUID across boots. Capacity queries deliberately omit UUIDs/serials. Per-BDF measured capacities and the final BDF-to-MiB map are retained in console/journal output and the private run events log. Reinspect BDF selection after any physical topology change.
