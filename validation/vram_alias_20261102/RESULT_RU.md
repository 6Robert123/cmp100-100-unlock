# VRAM alias run, 2026-11-02

Primary log: [full.log](full.log). Checker source: [../cuda_vram_alias_test.c](../cuda_vram_alias_test.c), SHA-256 `b3e8f8855b719c9f21841f85f76e14250208ca20bf8873dc3cdbc9ab9f84f172`. Built on the research host with `gcc -O2 -ldl` (gcc 13.3.0). NVIDIA driver queried immediately before the run: 580.178.04. No GPU UUID, board serial, hostname, or network address is in this note or the log.

Two powered CMP 100-100 devices were tested, one at a time, by setting `CUDA_VISIBLE_DEVICES` to the PCI BDF. `nvidia-smi` reported 12288 MiB on each. Both links were Gen1 x1 (`LnkSta: Speed 2.5GT/s, Width x1`). No compute process was using either GPU. A third CMP at `0000:02:00.0` was enumerated but had no power cables (`RmInitAdapter failed`) and made `cuInit` return 101. It was unbound from `nvidia` for the run and bound back afterwards (`REBIND_DONE`). That card was not tested.

## Quick run, immediately before the full log

| BDF | CUDA total | Tested | Result |
|---|---:|---:|---|
| `0000:01:00.0` | 12186 MiB | 11264 MiB | `PASS_QUICK FAILURES=0` |
| `0000:03:00.0` | 12186 MiB | 11264 MiB | `PASS_QUICK FAILURES=0` |

## Full run, 2026-11-02 05:58:50–06:02:44 +11:00

Each full run wrote two patterns across 44 segments of 256 MiB and read every 32-bit word back. Exit code 0 on both cards. The Xid section of the log is empty.

| BDF | CUDA total / free | Tested | Result | After: temp, power, used |
|---|---|---:|---|---|
| `0000:01:00.0` | 12186 / 11960 MiB | 11264 MiB | `PASS_FULL FAILURES=0` | 49 °C, 41.39 W, 0 MiB; later 51 °C, 41.88 W |
| `0000:03:00.0` | 12186 / 11960 MiB | 11264 MiB | `PASS_FULL FAILURES=0` | 40 °C, 23.15 W, 0 MiB; later 41 °C, 23.40 W |

`11264` is `(11960 - 512)` rounded down to a 256 MiB boundary. The checker does not fill the reserved 512 MiB or the gap between CUDA total (12186) and the `nvidia-smi` figure (12288). This run shows no alias or mismatch inside the tested 11264 MiB on these two cards. It does not cover the third, unpowered CMP.
