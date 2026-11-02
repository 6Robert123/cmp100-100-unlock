> Historical evidence snapshot. Some interpretations are superseded: read [review corrections](../../../../docs/REVIEW_CORRECTIONS.md) before using conclusions or commands. This is not an installation guide.

# Native MODS367 `Subdev.SetPcieLinkWidth` entry

## Result

The pinned MODS367 build registers the public name `SetPcieLinkWidth` with wrapper `0x7165c0`; its usage text says `Subdev.SetPcieLinkWidth(1/2/4/8/16)`. The wrapper resolves a `JsGpuSubdevice` / `GpuSubdevice` object, sends the supplied argument through generic parser `0x556340`, takes the resulting DWORD from `[RSP+0xc]`, and dispatches through an indirect virtual slot `[RAX+0x88]`. It then encodes the returned DWORD into the JS return value. The exact callee behind slot `+0x88` is unresolved.

The wrapper has no visible LnkCap/max-width check and does not compare the requested value against `{1,2,4,8,16}` before this indirect call. The usage string documents those choices; it is not a guard. The wrapper also requires a non-null global manager/object resolution and has an `EDX==1` branch to the usage error; the exact argument-count convention is not inferred here.

This closes the public-wrapper side, but not the key backend edge: the indirect C++ `GpuSubdevice` method is not bound here to `NV2080_CTRL_CMD_BUS_SET_PCIE_LINK_WIDTH`, an RM control call, or the GP100 BIF callback `0x11af740`. Therefore this evidence does not establish whether a width-16 request on the observed x1-cap endpoint would be rejected, clamped, or reach the GP100 setter. It does not yet justify GPU execution. A kernel-interface compatibility build would only supply an engineering prerequisite; it would not demonstrate a changed maximum-width capability or an x16 result.

The separate GP100 MODS BIF-table binding remains valid: callback `0x11af740` routes to helper `0x11af320`, which alters only `[22:20]` and preserves `[31:27]` in its observed request paths. No source edge in this pass connects the JS virtual call to that BIF callback.

## Byte anchors and replay

Input: MODS367 ELF `loaded_firmware_20261001/tool_inputs/cmp_analysis_mods367.unpacked`, SHA-256 `74daa3f6237d7fe88f4be102ae1f638b8d95e91d731f0eda09d1a991f9f820cb`, 66,066,632 bytes.

- Registration FDE `0x44f500..0x44f771`: at `0x44f744..0x44f76b`, loads user label pointer `0x26effa0`, wrapper address `0x7165c0`, API name pointer `0x26effb4`, then calls registration helper `0x5712d0`.
- JS wrapper FDE `0x7165c0..0x7166a1`: checks the wrapper state/arity, resolves object type labels at VAs `0x26ee893` (`JsGpuSubdevice`) and `0x270c162` (`GpuSubdevice`), calls parser `0x556340`, then indirect-dispatches via slot `+0x88`.
- String file offsets `0x22effa0`, `0x22effb4`, `0x22f3148` map through `.rodata` (file offset `0x228c040`, VMA `0x268c040`) to VAs `0x26effa0`, `0x26effb4`, `0x26f3148`, respectively.
- Existing public control-header evidence documents `NV2080_CTRL_CMD_BUS_SET_PCIE_LINK_WIDTH` (`0x20801804`), its `pcieLinkWidth` field and PSTATE/config-access/training error bits. That contract does not bridge the unresolved JS vtable callee to this RM command or supply a GP100-specific cap check; see `width_consumer_search` (archive reference: `../../evidence_20261029_cmp02_x16/width_consumer_search/RESULT_RU.md`; original context may be excluded).

Replay: `python3 evidence_20261030_cmp02_x16/native_mods_width_entry/replay.py` → `PASS_BOUNDED_NATIVE_WRAPPER`. The exact disassembly windows are saved in `registration_and_wrapper.disasm.txt` (archive reference: `registration_and_wrapper.disasm.txt`; original context may be excluded). Static/offline only; no API invocation, hardware access, or build.
