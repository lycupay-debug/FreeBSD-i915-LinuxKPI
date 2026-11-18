<!-- SPDX-License-Identifier: MIT -->

# Patch usage and compatibility

## Meteor Lake candidate: `0002-i915-freebsd-gem-shmem-page-restore.patch`

- Target source: FreeBSD drm-kmod 6.12.85 source tree used for the 15.2-BETA2 build.
- Target OS evidence: FreeBSD 15.2-BETA2, amd64.
- GPU evidence: Meteor Lake-P, PCI ID `8086:7d55`.
- Build command used: `make -j4 KMODS="dmabuf ttm drm i915"`.
- Candidate module SHA-256: `110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d`.
- Patch scope: FreeBSD shmem GEM objects with struct-page backing, WILLNEED retention, live reference, and valid pages. It does not cover TTM or LMEM.

From the root of a clean drm-kmod 6.12.85 source tree whose FreeBSD kernel sources match the target kernel, apply and build as follows:

```sh
git apply --check /path/to/0002-i915-freebsd-gem-shmem-page-restore.patch
git apply /path/to/0002-i915-freebsd-gem-shmem-page-restore.patch
make -j4 KMODS="dmabuf ttm drm i915" SYSDIR=/usr/src/sys
```

The documented candidate build was successful with the recorded environment. This command is not a promise of compatibility with other drm-kmod commits, FreeBSD kernel ABIs, or hardware.

Apply only after confirming the target source revision:

```sh
git apply --check /path/to/0002-i915-freebsd-gem-shmem-page-restore.patch
git apply /path/to/0002-i915-freebsd-gem-shmem-page-restore.patch
```

The patch is a source change, not a binary module. Build it against the exact target FreeBSD source tree, kernel configuration, DRM-kmod revision, and ABI. Do not distribute the recorded `.ko` as a portable binary.

## Original Skylake candidate: `0001-i915-skylake-restore-shmem-pages.patch`

This is the original hardware-guarded implementation, recorded for Intel Skylake-U HD Graphics 520 (PCI ID `8086:1916`) on FreeBSD 15.1-RELEASE-p6 and drm-612-kmod 6.12.85.1501000_2. One targeted post-reboot data-preservation run was recorded. It is not the recommended patch for Meteor Lake.

From a clean drm-kmod 6.12.85 source tree matching the target FreeBSD kernel:

```sh
git apply --check /path/to/0001-i915-skylake-restore-shmem-pages.patch
git apply /path/to/0001-i915-skylake-restore-shmem-pages.patch
make -j2 KMODS=i915 SYSDIR=/usr/src/sys
```

This command records the successful historical Skylake build configuration. It is not evidence that the Skylake patch applies to every drm-kmod 6.12.85 packaging revision or later kernel ABI.

## GuC is independent of these patches

Neither patch requires `hw.i915kms.enable_guc=1`. The change is in the FreeBSD shmem GEM page lifecycle and is independent of GuC firmware loading/submission. The maintainer's current MTL configuration has GuC 1 enabled, but it was enabled at the same time as the candidate patch; there is no isolated A/B evidence that it is necessary or responsible for the observed desktop behavior.

In upstream i915, `enable_guc` is a bitmask: `0` disables GuC/HuC, `1` enables GuC submission, `2` enables HuC loading, and `3` requests both. The default is automatic (`-1`) in the inspected driver source. Only configure GuC separately when the target GPU, firmware, and driver build support it. For GuC submission, add `hw.i915kms.enable_guc="1"` to `/boot/loader.conf` and reboot; verify the firmware/submission messages. This setting is not needed to apply or compile either patch.

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

The target upstream file `drivers/gpu/drm/i915/gem/i915_gem_pages.c` is marked `SPDX-License-Identifier: MIT` in Linux v6.12 and FreeBSD drm-kmod's drm_v6.12.85 branch. The patch does not replace that upstream license or remove Intel's copyright notice. Project-authored additions in the patch are licensed under MIT. See the repository's [LICENSE](../LICENSE), [MIT notice](../LICENSES/MIT.txt), and [MIT license](../LICENSES/MIT.txt).
