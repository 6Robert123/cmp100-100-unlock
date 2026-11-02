# Corrections and source precedence

Read this before historical reports. Reports are retained for their observations and provenance; their conclusions are not all endorsed. Earlier pause/handoff documents describe the state on their own date and may predate later experiments.

- **12 GiB:** NVIDIA enumeration is demonstrated. Complete VRAM correctness and physical memory topology are separate claims; a supplied November 2 log now contains two successful 11264-MiB full-mode runs; the remaining capacity and independent device-ID attribution are not covered.
- **Working helper source:** `.legacy.c` is not the source of the deployed ELF. New public wrapper validation does not retroactively make it the executed historical wrapper.
- **Latest x16 result:** advertised cap16 was obtained, actual link remained x1. Earlier reports saying cap16 never changed are superseded by the separated-MAX experiment.
- **PMU text:** `PMU Breakpoint` appeared next to baseline scalars before native writes. The label is not, by itself, proof of a PMU hardware breakpoint or causal explanation. Raw MODS RC1 remains a fact.
- **Unchanged LnkCap:** does not establish absolute fuses. Execution, later writers, timing, integrity and selection are alternatives.
- **Clean AER:** not proof of missing physical lanes, and not proof that software routes are exhausted.
- **LINK_CONFIG_0 bit4:** a persistent bit correlates with timeout and a nonzero low-five-bit polling condition; hardware acceptance and exact semantics are not established merely by comparing idle/transient reads.
- **IFR absence:** absence of a direct target address in inspected IFR records does not exclude computed/copy writes, another script, or another firmware path. A missing LnkCap2 record does not prove speed cannot be affected indirectly.
- **Timing:** a runtime failure does not exclude all preboot changes. The statement that all three goals have one uniquely proven early window remains a hypothesis.
- **PLM search:** literal scans and arbitrary-start disassembly are bounded searches, not proof that no writer exists. Computed/indirect register access remains outside that coverage.
- **Byte sum:** zero modulo256 in original IFRs is not a proven integrity algorithm or a basis for a guessed checksum repair. The two CMP inputs share the same IFR and are not independent samples.
- **Gen2 candidate:** clearing CYA/LINK_CONFIG_0 is not equivalent to P100's preserve-value masked records. It also changes fields other than a proven speed selector.
- **Root ports:** the retained baseline shows x16 maximum on the parents of both CMP02 and CMP03; CMP03's value is the independently acquired clip dump, not exclusive x16 support.
- **SPI identity:** Oct26 clip-dump metadata matches older CMP01/CMP03. Current CMP02 has a software NVFlash image; it is not an independent clip read.
- **HBM:** logical masks/geometry fields do not determine fitted die density or physical package mapping. 4 GiB/stack does not choose stack height. GV100 is Volta, not Pascal.

When a report conflicts with this file, retain the raw observation but use the narrower claim above. External architecture/library recommendations are not newly verified for this packaging task; archived source URLs retain their historical context.

## Public release audit correction

The initial scrub missed board serial fields, truncated identifiers and hostname-bearing paths. The initial validator checked integrity and links, not privacy. That PASS was insufficient for publication. The public copies have been re-scrubbed; a separate bounded pattern scanner is now included in validation. Historical multi-card shell wrappers are not endorsed. The initial public multi-card path was first disabled. The current revision replaces it with an offline-tested per-device candidate; a subsequent two-CMP hot apply trial passed, with mixed NVIDIA hosts and cold boot still untested on hardware. See [trial evidence](../validation/hardware_20261102/README.md). See MULTICARD_METHOD_REVIEW.md for the historical/new-method distinction.
