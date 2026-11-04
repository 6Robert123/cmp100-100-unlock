# 🔓 NVIDIA CMP 100-100 12GB unlock (GP100, Linux)


**12 GiB on Linux · Help finish Gen2 & x16**

Hi, I'm **6Robert123** 👋 I picked up an NVIDIA CMP 100-100 (GP100 / Pascal) second-hand for about $25. It's a mining card that ships with 6 GiB enabled, and I wanted to see how much more it could do. After a lot of digging it now comes up on Linux with **12,288 MiB** reported by the NVIDIA driver. A two-card hot `--apply` trial passed on 2 November 2026, and the historical evidence runs through 1 November 2026.

I'm still fighting the PCIe link, though. **Gen2 and physical x16 have not been achieved.** The fourth HBM site, physical die density, and stack height are not established either. If you know Pascal/GP100 PCIe or HBM internals, help is very welcome. (This is not the GV100 CMP100-210 project.)

## Quick start (30 seconds)

- **What:** initializes an NVIDIA CMP 100-100 (GP100) on Linux so the driver reports 12288 MiB instead of 6144 MiB.
- **Needs:** Linux x86_64, glibc >=2.38, NVIDIA driver, standard PCI tools, Python 3.8+.
- **Run:**

```sh
bash unlock/cmp100-12gb.sh --help
sudo bash unlock/cmp100-12gb.sh --check
sudo bash unlock/cmp100-12gb.sh --apply
```

Gen2 and physical x16 are not achieved. Review the prerequisites in [unlock/README.md](unlock/README.md) before `--apply`.

## Start here

- [Русская версия](README_RU.md)
- [12 GiB mechanism and reproducibility](docs/MEMORY_12GIB.md)
- [Single-file Linux/Ubuntu runner](unlock/README.md)
- [Gen2/x16 results and next research questions](docs/PCIE_ROADMAP.md)
- [Hardware photos of the modded card used for the width experiment](docs/PCIE_ROADMAP.md#hardware-photos-the-modded-card-used-for-the-width-experiment)
- [Corrections to historical conclusions](docs/REVIEW_CORRECTIONS.md)
- [Historical reports and captured evidence](research/INDEX.md)
- [Artifact provenance](research/PROVENANCE.jsonl)

## One file: mixed hosts and optional boot entry

The `.sh` contains the Python 3.8+ controller and pinned helper; no separate executable download is needed. Linux x86_64, glibc >=2.38, NVIDIA driver and standard PCI tools are required.

```sh
bash unlock/cmp100-12gb.sh --help
sudo bash unlock/cmp100-12gb.sh --check
# Explicit device-changing run, after reviewing prerequisites:
sudo bash unlock/cmp100-12gb.sh --apply
# Optional next-boot setup only; it does NOT run initialization now:
sudo bash unlock/cmp100-12gb.sh --install-boot
```

Default targets are all `10de:15c2`; optional BDFs select a subset. P100, other NVIDIA and AMD presence is allowed. The controller never unloads the shared NVIDIA modules and sends unbind/reset/config/override/bind writes only to selected CMPs. A bus-reset target sharing its reset bridge is refused. Active selected-GPU clients or unattributable shared NVIDIA clients are refused, so this does not promise uninterrupted hot operation alongside arbitrary workloads.

During `--boot`, a missing NVIDIA core may be loaded once; normal driver registration may attach other previously unbound NVIDIA devices. Already bound non-target bindings must remain unchanged. No reset/helper is directed to those other devices. [Full instructions and limits](unlock/README.md).

**A fresh two-CMP hot `--apply` hardware trial passed on 2 November 2026: both cards changed from 6144 to 12288 MiB, exit 0.** [Exact script, logs, initial noexec failure and coverage limits](validation/hardware_20261102/README.md). Mixed NVIDIA hosts and cold boot remain hardware-unvalidated. The archived service worked on the research host, but used a different global unload/reload loop. Its success is not a hardware test of this release. The source of the embedded helper is now published: [`gp100_cfg1_race_optfbio.c`](unlock/historical/gp100_cfg1_race_optfbio.c). Its functions, constants and output strings match the shipped ELF; build it yourself with `gcc -O2 -o gp100_cfg1_race gp100_cfg1_race_optfbio.c`. Byte-identical output needs the original toolchain (GCC 13.3, Ubuntu 24.04). `.legacy.c` is an older, different implementation. [Details](docs/MEMORY_12GIB.md).

## What the evidence supports

| Topic | Result | Limit |
| --- | --- | --- |
| Memory initialization | Multiple archived NVIDIA snapshots report 12,288 MiB after CFG1 initialization | Does not prove all VRAM addresses are error-free |
| CUDA memory checker | Two supplied labeled runs: `PASS_FULL`, zero reported failures, 11264 MiB each | Reserved/unallocated memory and checker device-ID association are not independently covered; [log review](validation/vram_alias_20261102/REVIEW_RU.md) |
| Advertised x16 | `LnkCap` changed from `00453c11` to `00453d01` in one post-init experiment | Negotiated link stayed Gen1 x1 |
| Native width command | One bounded command2 timed out; 450 paired samples remained Gen1 x1 | Does not prove physical-only cause or exhaust preboot routes |
| SPI candidates | Deterministic operand patches, original/candidate hashes available | Not flashed; IFR execution/integrity unresolved |

## Repository boundaries

Includes research reports, limited redacted run captures, original project code and the small historical helper ELF. No NVIDIA driver/MODS distributions, complete firmware/SPI dumps, credentials, or private correspondence. Bring your own matching inputs for offline parsers. Historical code is evidence, not a recommended experiment queue.

## Offline checks

Run `python3 tools/check_bundle.py` to verify hashes, embedded helper identity and local documentation links. [Recorded validation](validation/OFFLINE_VALIDATION.json) includes local-only SPI rebuild checks and owner-check fixtures. These checks are offline. The separate [two-CMP hot apply trial](validation/hardware_20261102/README.md) and [supplied VRAM runs](validation/vram_alias_20261102/REVIEW_RU.md) document runtime evidence and its limits.

## Research discussions

Discussion priorities are a reproducible build of the helper, full-capacity VRAM validation, and the GP100 link-training path. Original code contributions are welcome under MIT. See [CONTRIBUTING.md](CONTRIBUTING.md). Please include failed trials and restoration status as well as successful output.

Original project code and documentation are licensed under [MIT](LICENSE), copyright 2026 **6Robert123**. Historical archive and embedded-helper exclusions are described in [NOTICE.md](NOTICE.md).

Identifying fields in evidence copies are redacted ([data/provenance policy](docs/DATA_POLICY.md)).

## Support the project 🐕

If this project helped you, you can toss some Dogecoin my way:

`DJtYtCiA7Q3Us3nwrH8Sj8aVNpHBhUFDxH`
