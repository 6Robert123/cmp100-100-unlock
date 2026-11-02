# 12 GiB initialization: measured scope and mechanism


Author: 6Robert123. The archived deployment uses a Linux user-space helper during NVIDIA initialization. This is a volatile initialization override, replayed at boot/reinitialization, not an SPI flash modification.

## Pinned implementation

The archived deployed Linux x86_64 helper is `unlock/historical/gp100_cfg1_race.x86_64.elf`:

```
SHA256 a1ca427cb84f3225a5d337ebeaed20cdf1673d6ea7a5ee68a0dacf2bc8d88de6
```

Its exact source/build recipe was not found in the searched project and adjacent archives. The historical `gp100_cfg1_race.legacy.c` hardcodes FBPA indices and is explicitly a different implementation. Do not compile it and label the result the deployed helper. The binary and disassembly are retained to allow review of what actually ran; they are not a source-reproducible release.

## Static software path

The pinned helper maps 16 MiB of `/sys/bus/pci/devices/<BDF>/resource0` read/write, reads `BAR0+0x21c14`, and uses its bits as a skip selector. For clear-bit indices among 0..15, its writer examines CFG1 DWORDs at `0x900204 + index*0x4000`. The archived wrapper passes target `0x02669000`; the trigger comparison is `0x02559000`. The helper conditionally replaces the CFG1 values during initialization.

In the audited helper, `0x21c14` is read, not programmed. The audit does not find a write to `0x21938` either. Thus the helper is not evidence of enabling a fourth memory interface or setting the source floorsweep mask.

See the [deployed ELF audit](../research/archive/secondary-host/GP100_CFG1_RACE_ELF_AUDIT_20261026_RU.md) and [bounded disassembly](../research/archive/secondary-host/GP100_CFG1_RACE_ELF_RAW_20261026.txt).

## Demonstrated result and missing validation

The [October 22 repair record](../research/archive/NVIDIA_REPAIR_RESULT_20261022_RU.md) records NVIDIA 580.178.04 and CMP/P100/CMP capacities 12288/16384/12288 MiB after reboot. The [November 1 baseline](../research/archive/evidence_20261031_plan_spi_route/baseline_pre_flash_20261101.txt) is a retained primary output with three CMP devices reporting 12288 MiB, all still Gen1 x1. These are archived measurements, not fresh hardware tests performed for this publication.

`nvidia-smi` capacity alone does not prove every address works without aliasing/corruption. The CUDA checker source is included in `validation/cuda_vram_alias_test.c`. The newly supplied [full log](../validation/vram_alias_20261102/full.log) contains two labeled runs, each with 88 zero-failure progress rows and `PASS_FULL FAILURES=0 TESTED_MIB=11264`, rc0. These are successful tested-range results, not merely capacity enumeration. Earlier failed exploratory routes remain separate. Reserved/unallocated memory is not covered; the checker does not print its PCI identity, so BDF association comes from operator labels. See [review](../validation/vram_alias_20261102/REVIEW_RU.md).

The original wrapper could return success when only one card passed and tolerated some failures. The historical adapted wrapper checks the final snapshot for all cards. Neither historical wrapper should be installed blindly. The new public wrapper subsequently passed a [fresh two-CMP hot apply trial](../validation/hardware_20261102/README.md); mixed NVIDIA hosts and cold boot still require hardware validation.

## Portability limits

The retained helper is an x86_64 Linux ELF, not an ARM or macOS executable. The observed driver versions include 580.173.02 and 580.178.04; compatibility with other kernels, drivers, firmware, reset topologies, or devices sharing an identifier is not guaranteed. Dedicated cooling, an idle host, usable PCI reset, and a clean NVIDIA/nouveau startup order are prerequisites. Boot persistence is optional: explicit `--install-boot` installs a managed systemd entry for subsequent boots, without starting it now or changing module blacklists. A legacy enabled initializer causes refusal until the owner resolves duplicate startup.
