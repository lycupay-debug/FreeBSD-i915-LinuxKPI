<!-- SPDX-License-Identifier: BSD-2-Clause -->

# Patch usage and compatibility

## Meteor Lake candidate: `0002-i915-freebsd-gem-shmem-page-restore.patch`

- Target source: FreeBSD drm-kmod 6.12.85 source tree used for the 15.2-BETA2 build.
- Target OS evidence: FreeBSD 15.2-BETA2, amd64.
- GPU evidence: Meteor Lake-P, PCI ID `8086:7d55`.
- Build command used: `make -j4 KMODS="dmabuf ttm drm i915"`.
- Candidate module SHA-256: `110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d`.
- Patch scope: FreeBSD shmem GEM objects with struct-page backing, WILLNEED retention, live reference, and valid pages. It does not cover TTM or LMEM.

Apply only after confirming the target source revision:

```sh
git apply --check /path/to/0002-i915-freebsd-gem-shmem-page-restore.patch
git apply /path/to/0002-i915-freebsd-gem-shmem-page-restore.patch
```

The patch is a source change, not a binary module. Build it against the exact target FreeBSD source tree, kernel configuration, DRM-kmod revision, and ABI. Do not distribute the recorded `.ko` as a portable binary.

## Original Skylake candidate: `0001-i915-skylake-restore-shmem-pages.patch`

This is the original hardware-guarded implementation, recorded for Intel Skylake-U HD Graphics 520 (PCI ID `8086:1916`) on FreeBSD 15.1-RELEASE-p6 and drm-612-kmod 6.12.85.1501000_2. One targeted post-reboot data-preservation run was recorded. It is not the recommended patch for Meteor Lake.

## Regression test

Build with:

```sh
make -C tests clean all
```

Run the script only as root on a recoverable test system. It triggers global `DROP_UNBOUND`; it is not a harmless unit test and should not be run during important desktop workloads.

```sh
ITERATIONS=1 EXPECTED_SHA256=<candidate-module-sha256> sh tests/run-gem-wb-shmem-test.sh
```

The runner's hash check identifies the on-disk module file only. It cannot cryptographically identify the already-loaded in-memory kernel module. A passing run must be recorded with OS release, kernel ABI, GPU PCI ID, DRM-kmod package/source revision, module file hash, loaded-module evidence, iteration count, full test output, and relevant dmesg before/after.

## Current evidence boundary (2026-11-18)

The maintainer's Meteor Lake laptop currently has `/boot/modules/i915kms.ko` with the candidate SHA-256 above, and `kldstat` reports that module loaded from this path after the latest boot. This is strong boot-time evidence, not an in-memory cryptographic measurement. The targeted GEM regression has not yet been rerun against this loaded candidate. Do not claim runtime regression success until a fresh test reports `RESULT=DATA_PRESERVED`.

The patch and GuC 1 were enabled together during a period without the previously observed display corruption. This observation cannot isolate causation. RCS0 GPU hangs remain a separate, unresolved investigation.

## Recovery

Preserve the original module, its checksum, and a tested recovery path before replacing any kernel module. Do not automatically unload, replace, or reboot as part of applying this patch. If the desktop fails to start, use the local console/recovery procedure and restore the exact known-good module.


## Licensing

The target upstream file `drivers/gpu/drm/i915/gem/i915_gem_pages.c` is marked `SPDX-License-Identifier: MIT` in Linux v6.12 and FreeBSD drm-kmod's drm_v6.12.85 branch. The patch does not replace that upstream license or remove Intel's copyright notice. Project-authored additions in the patch are licensed under BSD-2-Clause. See the repository's [LICENSE](../LICENSE), [MIT notice](../LICENSES/MIT.txt), and [BSD-2-Clause license](../LICENSES/BSD-2-Clause.txt).
