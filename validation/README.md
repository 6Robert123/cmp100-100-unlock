# Memory checker and supplied results

The source `cuda_vram_alias_test.c` uses the CUDA Driver API through `dlopen`; CUDA toolkit headers are not required. A compatible installed NVIDIA driver, a compiler, and libdl are needed. Build on Linux from the repository root:

```sh
cc -O2 -Wall -Wextra validation/cuda_vram_alias_test.c -o /tmp/cmp100-vram-check -ldl
```

For a future explicitly chosen test on an idle GPU, select a valid full GPU UUID from your local `nvidia-smi -L` output (do not publish that identifier):

```sh
CUDA_VISIBLE_DEVICES='<GPU_UUID>' /tmp/cmp100-vram-check --quick
CUDA_VISIBLE_DEVICES='<GPU_UUID>' /tmp/cmp100-vram-check
```

NVIDIA documents integer CUDA ordinals or GPU UUIDs for [CUDA_VISIBLE_DEVICES](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/environment-variables.html); a raw PCI BDF is not a documented selector. The checker uses visible CUDA device 0, allocates most free VRAM, and performs destructive writes to its own allocation. It does not alter firmware or automatically unbind/reset any adapter. Do not combine it with active workloads. Observe cooling; the checker has no built-in temperature cutoff.

Full mode fills all allocated 256-MiB segments and reads every word back with two pattern passes. Quick mode samples only the first and last word of each segment after writing the allocation. Passing quick mode is not a full scan. The checker reserves at least 512 MiB and rounds down, so passing full mode still does not cover every byte of reported board memory.

The newly supplied [runtime log and review](vram_alias_20261102/REVIEW_RU.md) contain two successful full-mode 11264-MiB runs. The original checker source and supplied log/note are preserved; log timestamps were normalized to a neutral timezone (+11:00). Exact build/launch provenance and independent PCI identity output were not supplied. No GPU memory test was run by the release packager.

Offline checks: `python3 -B validation/audit_vram_run.py`, `python3 -B validation/test_multicard_engine.py`, and `python3 -B validation/test_release_guards.py`.

## Fresh initializer trial

[Two-CMP hot apply, 2 November 2026](hardware_20261102/README.md): both devices 6144 → 12288 MiB, exit 0. Includes initial noexec failure, recovery and fix; not a cold-boot, mixed-NVIDIA-host or CUDA stress test.
