# CMP 100-100 UNLOCK — two-CMP hot apply tested, 2 November 2026


By 6Robert123. A Linux/Ubuntu single-file initializer around the archived 12 GiB helper, its static audit, selected GP100 Gen2/x16 experiments, and an evidence-backed roadmap for contributors.

NVIDIA-reported 12,288 MiB initialization is documented. Two supplied full-mode memory runs each passed over 11264 MiB; reserved/unallocated memory and independent BDF attribution remain outside the retained evidence. Advertised x16 was achieved in a post-init trial, but negotiated link stayed Gen1 x1; Gen2 is also unfinished. No permanent firmware unlock, fourth-stack unlock or universal compatibility is claimed.

The new multi-CMP shell/Python wrapper has CPU fixtures and one successful two-CMP hot apply trial. The pinned historical helper is included unchanged; matching source has not been recovered. Full ROMs, NVIDIA tool binaries and private chat histories are omitted. Original project code and documentation are now under [MIT](../LICENSE); historical archive and embedded-helper exclusions are detailed in [NOTICE.md](../NOTICE.md).

New: per-CMP processing without NVIDIA module unload, mixed-adapter inventory/protection, and explicit next-boot installer/uninstaller. CPU fixtures cover 3/16 CMPs plus non-target adapters and failure cleanup. The revised script passed a [two-CMP hot apply trial](../validation/hardware_20261102/README.md), after fixing `/run` noexec handling. Both cards report 12288 MiB. Cold boot and mixed NVIDIA host behavior still need hardware validation.
