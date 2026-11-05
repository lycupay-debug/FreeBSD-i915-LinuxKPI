# FreeBSD-i915-LinuxKPI

Experimental FreeBSD DRM/i915 workaround for a reproducible GEM backing-page data-loss condition observed on Intel Skylake (HD Graphics 520, PCI ID `8086:1916`) under FreeBSD 15.1-RELEASE-p6 with `drm-612-kmod 6.12.85.1501000_2`.

**Status: experimental; targeted local regression passed once after reboot. Not an upstream fix, not a general LinuxKPI repair, and not yet proven stable under long-duration workloads.**

## Problem

A 4 MiB GEM object was marked `I915_MADV_WILLNEED`, mapped using `I915_MMAP_OFFSET_WB`, and filled with a deterministic 64-bit pattern. After triggering i915 `DROP_UNBOUND` through debugfs, the object still reported retained, but reading the mapping returned zero instead of the original first word (`0x9e3779b97f4a7c15`). The unmodified driver reproduced `RESULT=DATA_LOSS` twice on the Skylake test laptop.

This is consistent with the class of issue discussed in [FreeBSD Bugzilla 298279](https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=298279), but that report concerned a different Intel generation. This repository records a separate local reproduction on Skylake.

## Working mechanism

Source inspection identified this likely sequence in the FreeBSD DRM/LinuxKPI path:

1. i915 obtains backing pages from the GEM shmem VM object.
2. A WB mmap-offset fault maps pages through the FreeBSD cdev device-pager path.
3. LinuxKPI page insertion transfers the page association from the original shmem object to a device-pager object.
4. On unmap, the device-pager association is removed but the original shmem association is not restored.
5. GEM shrink/put-pages can then release a detached page.
6. A later shmem fault can allocate a zero-filled replacement page while the object remains marked WILLNEED.

This explanation is supported by the local reproducer and source-path inspection, but is not a formal proof for every workload or DRM backend.

## Patch

`patches/0001-i915-skylake-restore-shmem-pages.patch` adds a FreeBSD-only helper in `drivers/gpu/drm/i915/gem/i915_gem_pages.c`. It runs after mmap-offset unmapping and before the GEM backend drops pages.

The helper is scoped to Skylake shmem GEM objects with struct-page backing, WILLNEED retention, a live reference, and valid backing pages. It checks the original shmem VM object slots, reinserts detached pages, and marks them dirty. It refuses to overwrite another resident page or take ownership of a page still associated with another object. Unsafe restoration returns an error.

The patch deliberately does not widen support to Coffee Lake, other Intel generations, non-shmem objects, or other GEM backends. Each expansion requires independent reproduction and validation.

## Local evidence

Test platform: FreeBSD 15.1-RELEASE-p6 (amd64), Intel Skylake-U HD Graphics 520 (`8086:1916`), `drm-612-kmod 6.12.85.1501000_2`, Mesa 26.2.2, libdrm 2.4.134.

| Driver | WB mmap / WILLNEED / DROP_UNBOUND test |
|---|---|
| Original module | `DATA_LOSS`, reproduced twice |
| Candidate patched module | `DATA_PRESERVED`, one post-reboot run |

Patch dry-run succeeded against the exact local drm-kmod source. Build command:

```sh
make -j2 KMODS=i915 SYSDIR=/usr/src/sys
```

Recorded build result: `BUILD_EXIT=0`; `i915kms.ko` linked successfully. Candidate module SHA-256:

```text
fda3d77989589ba16db96b554eb47ebded022525b1a95a3bac1456dc09da55e6
```

Patch SHA-256:

```text
3dd8796a883418bfcabf55bdef1e4e31448b7888858763acaa877de919cec5a3
```

After a manual reboot, the candidate hash matched, the patched `i915kms.ko` was loaded, Skylake i915 initialization and DMC firmware loading succeeded, LightDM was running, and the same regression test returned `RESULT=DATA_PRESERVED` with exit status 0.

**This does not prove that historical RCS0 GPU hangs are eliminated.** GPU HANG events were observed, but their complete causal relationship to this GEM data-loss defect remains unproven. Long-duration use, memory pressure, repeated shrink cycles, multiple objects, and error-path robustness remain unverified.

## Contents

- `patches/`: kernel source patch
- `tests/gem_wb_shmem_roundtrip.c`: standalone reproducer/regression test
- `tests/run-gem-wb-shmem-test.sh`: helper to mount lindebugfs and run the test
- `docs/INVESTIGATION.md`: incident timeline, code-path analysis, validation boundaries, and recovery notes

## Build the test program

On the target FreeBSD system with DRM/libdrm headers installed:

```sh
cc -O2 -Wall -I/usr/local/include tests/gem_wb_shmem_roundtrip.c -L/usr/local/lib -ldrm -o tests/gem_wb_shmem_roundtrip
```

Run only on a test machine, as root, with no valuable unsaved work:

```sh
su -
sh tests/run-gem-wb-shmem-test.sh
```

The test writes to i915 debugfs `i915_gem_drop_caches` and deliberately triggers the DROP_UNBOUND shrink path.

## Safety

- Experimental kernel-driver code; a bad module can prevent graphical startup or destabilize the machine.
- Do not apply to unrelated hardware or FreeBSD/drm-kmod versions without review and testing.
- Build against the exact target FreeBSD source tree, kernel configuration, and drm-kmod revision. The recorded `.ko` hash is not a portable binary guarantee.
- Preserve a known-good original module and recovery path before installing.
- Package upgrades/reinstalls may overwrite a locally installed module.
- No warranty; use at your own risk.

## References

- [FreeBSD Bugzilla 298279](https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=298279) — related device-pager/GEM backing-page report.
- [freebsd/drm-kmod issue 527](https://github.com/freebsd/drm-kmod/issues/527) — reported RCS0 preemption timeout / GPU hang.
- [freebsd/drm-kmod issue 509](https://github.com/freebsd/drm-kmod/issues/509) — another reported RCS0 timeout.
- [freebsd/drm-kmod](https://github.com/freebsd/drm-kmod) — upstream FreeBSD DRM port.

## License

GPL-2.0-only. See `LICENSE`. The patch modifies Linux i915 code and is distributed under GPL-2.0-only. The repository uses the same license for the standalone reproducer and documentation for simplicity.

## Second investigation: i915 GTT special-PTE handling

A second, separate investigation documents two kernel panics on 2026-11-05 during an assistant-started FFmpeg VA-API hwupload test. Both vmcores show the i915 GTT fault path entering the ordinary LinuxKPI PFN insertion function with a device-I/O PFN. Offline KGDB analysis mapped the PFN to the Intel GMADR aperture and to an invalid dense vm_page_array slot.

The candidate fix adds a LinuxKPI/FreeBSD VM special-PTE API backed by explicitly owned fictitious pages, then switches the FreeBSD i915 remap_pfn() and remap_sg() callers to that API. This work is independent of the existing GEM shmem-page restoration patch.

**Current status: candidate patch series; source-context dry run passed; no kernel build, module build, installation, load, or runtime validation has been performed.** The upstream D59883 review remains a prototype marked Needs Review in the status snapshot recorded on 2026-11-05. The local adaptation also addresses fake-page busy-lock lifecycle issues found during source review.

Detailed records:

- docs/gtt-special-pte/ROOT_CAUSE.md — incident timeline, vmcore hashes, panic stacks, PFN/GMADR arithmetic, and root-cause evidence.
- docs/gtt-special-pte/UPSTREAM_ANALYSIS.md — Linux special-PTE semantics, D59883/D59506/D53412/D58320 comparisons, and prototype lifecycle review.
- docs/gtt-special-pte/PATCH_DESIGN.md — API design, patch split, object/page lifecycle, busy-lock state transitions, and review questions.
- docs/gtt-special-pte/VALIDATION.md — completed checks, explicit unverified status, and staged build/runtime validation plan.
- docs/gtt-special-pte/RECOVERY.md — Git recovery, patch application order, and source-only reversal.
- compatibility/FREEBSD-15.1-DRM-6.12.85.md — exact target compatibility baseline.
- patches/gtt-special-pte/ — independent LinuxKPI/VM and i915 caller patches.

Do not run the previous FFmpeg hwupload test on the active installation. Do not install or load these candidate patches until the source review, build validation, and a separate deployment decision are complete.

- tests/gtt-special-pte/check-patches.sh — non-mutating patch-context dry-run for the FreeBSD and drm-kmod source roots.
