# Contributing

Original code patches, pull requests, discussions and measurement reports are welcome. By submitting original code or documentation for incorporation, you agree to license your contribution under the project's [MIT License](LICENSE). Keep required third-party notices, identify the source/license of reused material, and submit only material you have permission to contribute. Historical evidence and the embedded helper exclusions are described in [NOTICE.md](NOTICE.md).

Please identify the architecture first: GP100 CMP100-100, not GV100 CMP100-210. Include firmware/version/hash, driver/kernel, reset topology, configured memory, baseline link state and exact source revision. Use anonymous board labels instead of GPU UUIDs/serials.

For PCIe claims, include both endpoint and upstream LinkSta, elapsed observation window, workload checks, AER/Xid coverage, command return status and final restoration. Advertised capability alone is not link success. For memory, include actual tested byte ranges, patterns/readback and alias checks; `nvidia-smi` capacity alone is insufficient.

Priorities: recover the exact working helper source; validate the full usable VRAM range; prove the IFR execution/integrity contract; obtain GP100-specific per-lane or command-completion evidence. Report negative trials with the same precision. Distinguish software-derived inference from hardware observation.

Historical scripts are not default commands. Keep live-device tools separate from offline parsing. Avoid automatic service stops/reboots/flash and shared-bus resets. Add a new experiment only when it resolves a named uncertainty. No OTP programming or fuse burning belongs in the public initializer.

## Maintainers: rebuild this public snapshot


The portable builder is shipped inside the repository:

```sh
python3 -B tools/check_bundle.py
python3 -B tools/build_release.py --output-dir ../release-output
```

It packages only manifest-listed public files and verifies the ZIP. No private archive or former parent-folder scripts are needed. For deliberate maintainer edits, inspect the changes first, then run `python3 -B tools/refresh_manifest.py` to renew accepted-byte hashes before validation. Hash renewal is not a privacy check.
