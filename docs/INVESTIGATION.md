# Investigation record

## Incident observations (2026-11-04)

The laptop recorded repeated Intel i915 RCS0 GPU hang messages: `drmn0` GPU HANG (`ecode 9:1`), `Resetting rcs0 for preemption time out`, later Xorg/MainThread GPU HANGs, and service termination followed by reboot. Available logs did not identify the reboot initiator; no matching new vmcore/core.txt was found. It is therefore unknown whether a GPU hang directly caused the reboot.

The Intel RCS0 failures continued after the NVIDIA 930M driver/configuration had been removed. NVIDIA was not a necessary condition for these Intel failures.

## Platform

- FreeBSD 15.1-RELEASE-p6, `kern.osreldate=1501000`
- Intel Skylake-U HD Graphics 520, PCI ID `8086:1916`
- `drm-612-kmod 6.12.85.1501000_2`
- Mesa 26.2.2; libdrm 2.4.134

## Direct reproduction

The test creates a 4 MiB GEM object through `/dev/dri/renderD128`, marks it `I915_MADV_WILLNEED`, obtains `I915_MMAP_OFFSET_WB`, maps it, and writes a deterministic 64-bit pattern. It then writes bit 0 (`DROP_UNBOUND`) to `i915_gem_drop_caches`, checks that WILLNEED remains retained, and rereads the mapping.

With the original module, initial write/read passed, DROP_UNBOUND triggered, and WILLNEED remained retained; however, the first word changed from `0x9e3779b97f4a7c15` to zero. `RESULT=DATA_LOSS` was reproduced twice. The test does not submit GPU command buffers; it targets GEM CPU mapping/backing-page lifecycle.

## Source-level working model

Reviewed paths included `i915_gem_mman.c`, `i915_gem_pages.c`, LinuxKPI `linux_page.c`, and FreeBSD `sys/vm/device_pager.c`. WB mmap-offset fault handling maps a page through a device-pager object. LinuxKPI transfers its VM-object association away from the original shmem object. On unmap, the device-pager page is removed without restoring the original shmem association. GEM shrink/put-pages may then release a page no longer resident in the original backing object. A later shmem fault can allocate a zero-filled replacement.

This model matches the local reproduction and code inspection; its scope is limited to the tested FreeBSD and drm-kmod implementation.

## Patch behavior

The helper is called after `i915_gem_object_release_mmap_offset(obj)` and before backend page release. It requires Skylake, shmem GEM, struct-page backing, WILLNEED, a live object reference, and valid pages. Under the backing VM-object lock it checks each expected page slot, skips pages already resident at the expected index, refuses to overwrite another resident page, requires exclusive busy acquisition and `page->object == NULL`, reinserts detached pages, marks them dirty, and returns an error if restoration cannot be done safely.

## Build and verification

The patch passed `patch -p1 --dry-run` against the exact local drm-kmod source tree. Build command:

```sh
make -j2 KMODS=i915 SYSDIR=/usr/src/sys
```

Recorded result: `BUILD_EXIT=0`; `i915kms.ko` linked. After manual reboot, module SHA-256 matched the candidate; `kldstat` showed patched `i915kms.ko` loaded; Skylake i915 initialized and DMC firmware loaded; LightDM was running. The same regression test returned `RESULT=DATA_PRESERVED`, `TEST_EXIT=0`.

This is one successful post-reboot regression run, not proof of multi-day stability or proof that all RCS0 GPU hangs are fixed. Long-duration desktop workloads, memory pressure, repeated shrink cycles, multiple GEM objects, and exceptional error paths remain unverified.

## Recovery cautions

Keep an original known-good `i915kms.ko`, verify its checksum, and preserve a recovery script before installation. Build against the exact target source/configuration/revision. Ensure local console or alternate recovery access. Replacing a module on disk does not replace the already-loaded module; reboot manually when ready. Package upgrades/reinstalls may overwrite a manually patched module. Do not treat this as a maintained package until reproducible packaging is implemented.
