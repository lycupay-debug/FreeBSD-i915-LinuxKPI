<!-- SPDX-License-Identifier: MIT -->
# 详细报告
https://www.lycupaynotion.top/content/i915
# FreeBSD-i915-LinuxKPI

Experimental, FreeBSD-specific i915 GEM shmem backing-page restoration work and regression tooling for a reproducible data-integrity failure involving WB mmap-offset mappings and the `DROP_UNBOUND` shrinker path.

**Status: experimental candidate. The patch applies and builds against the documented DRM-kmod source. The Meteor Lake candidate module is present and loaded on the maintainer's laptop, but the GEM regression test has not yet been rerun against that loaded candidate. This is not an upstream fix or a generally supported driver package.**

## Public repository and quick checkout

The repository is public. The default `main` branch is intended to contain the current documented candidate and test harness.

```sh
git clone https://github.com/lycupay-debug/FreeBSD-i915-LinuxKPI.git
cd FreeBSD-i915-LinuxKPI
git status --short --branch
```

To inspect the exact Meteor Lake candidate patch:

- Patch: [patches/0002-i915-freebsd-gem-shmem-page-restore.patch](patches/0002-i915-freebsd-gem-shmem-page-restore.patch)
- Investigation record: [docs/INVESTIGATION.md](docs/INVESTIGATION.md)
- Patch usage and compatibility notes: [patches/README.md](patches/README.md)

## What the patch addresses

The investigation reproduced a condition where a 4 MiB GEM object marked `I915_MADV_WILLNEED`, mapped with `I915_MMAP_OFFSET_WB`, and populated with a deterministic pattern lost data after the i915 `DROP_UNBOUND` shrinker was triggered. The object continued to report retained state, but reads returned zeroes.

The working model is a page-ownership/lifecycle mismatch between the i915 shmem GEM backend, LinuxKPI, FreeBSD VM objects, and the cdev device-pager teardown path. It remains a source-supported working model, not a formal proof for every workload or backend.

## Patch variants

| File | Intended scope | Evidence status |
|---|---|---|
| `patches/0001-i915-skylake-restore-shmem-pages.patch` | Original Skylake-only implementation | One successful post-reboot targeted data-preservation run was recorded |
| `patches/0002-i915-freebsd-gem-shmem-page-restore.patch` | Meteor Lake port; removes the Skylake-only hardware guard while retaining shmem/state checks | Applies and builds; loaded on maintainer's Meteor Lake laptop; targeted runtime regression still pending |

The Meteor Lake patch does not cover TTM or LMEM backends. Do not assume support for other Intel generations, FreeBSD releases, drm-kmod revisions, or GEM backends without independent review and testing.

## Compatibility and build evidence

The Meteor Lake candidate was prepared against:

- FreeBSD 15.2-BETA2, amd64
- Intel Core Ultra 5 125H / Meteor Lake-P integrated graphics, PCI ID `8086:7d55`
- drm-kmod 6.12.85 source, package baseline recorded as `drm-612-kmod-6.12.85.1502000_3`
- Candidate build command: `make -j4 KMODS="dmabuf ttm drm i915"`
- Candidate `i915kms.ko` SHA-256: `110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d`

On 2026-11-18, the maintainer's laptop reported the same SHA-256 for `/boot/modules/i915kms.ko`; `kldstat` showed `i915kms.ko` loaded from that path, and the machine had booted after the module file's recorded modification time. This is strong evidence that the candidate was loaded at boot, but it is not a cryptographic measurement of the in-memory kernel image.

**Not yet verified:** a fresh `RESULT=DATA_PRESERVED` run on the Meteor Lake laptop with the currently loaded candidate; repeated shrink cycles; memory-pressure stress; concurrent mmap/unmap; multiple-object behavior; and exceptional error-path stress.

A prior one-day observation without the previously reported display corruption occurred while both this patch and GuC 1 were enabled. Since both variables changed together, the observation cannot attribute the improvement to either one. It does not establish that RCS0 hangs are fixed.

## Apply the patch to a matching source tree

Use a clean DRM-kmod source tree matching the documented 6.12.85 base. Do not apply this patch to an arbitrary Linux kernel or a different drm-kmod revision.

From the root of the matching DRM-kmod source tree:

```sh
git apply --check /path/to/FreeBSD-i915-LinuxKPI/patches/0002-i915-freebsd-gem-shmem-page-restore.patch
git apply /path/to/FreeBSD-i915-LinuxKPI/patches/0002-i915-freebsd-gem-shmem-page-restore.patch
```

Or, using the traditional patch utility:

```sh
patch -p1 --dry-run < /path/to/FreeBSD-i915-LinuxKPI/patches/0002-i915-freebsd-gem-shmem-page-restore.patch
patch -p1 < /path/to/FreeBSD-i915-LinuxKPI/patches/0002-i915-freebsd-gem-shmem-page-restore.patch
```

Build from the DRM-kmod source root using the target system's matching FreeBSD source tree and kernel configuration:

```sh
make -j4 KMODS="dmabuf ttm drm i915" SYSDIR=/usr/src/sys
```

The command is a record of the successful candidate build environment, not a guarantee that it will build unchanged against other source revisions or kernel ABIs. Review [patches/README.md](patches/README.md) before installing anything.

## Build and run the regression test

On FreeBSD with the DRM/libdrm development headers and `pkg-config` installed:

```sh
make -C tests clean all
sh -n tests/run-gem-wb-shmem-test.sh
```

The runner must be executed as root (use `su -`) and requires a working i915 device plus the `lindebugfs` interface:

```sh
su -
cd /path/to/FreeBSD-i915-LinuxKPI
ITERATIONS=1 EXPECTED_SHA256=110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d sh tests/run-gem-wb-shmem-test.sh
```

**Safety:** the test writes to `i915_gem_drop_caches` and triggers `DROP_UNBOUND`, which affects the global i915 GEM shrinker, not only the test object. Run only on a recoverable test machine, outside important desktop workloads, with a known-good module and rollback path. The module-file hash check verifies the on-disk file only; it does not prove the identity of an already-loaded in-memory module.

The test does not submit GPU command buffers. It validates CPU-visible GEM backing-page retention, not GPU scheduling, rendering correctness, or RCS0 hang behavior.

## GuC configuration

GuC is **not a prerequisite for either GEM shmem page-restoration patch**. The patch addresses CPU-visible GEM backing-page ownership and restoration; it does not implement GuC firmware loading or GuC command submission.

The maintainer's current Meteor Lake setup records `hw.i915kms.enable_guc="1"`, which selects GuC submission in the upstream i915 parameter bitmask. This was enabled alongside the candidate patch during a period without the previously observed display corruption, so that observation does not prove GuC 1 is required by the patch or that either change caused the improvement.

Leave GuC at the target driver's default unless GuC submission is independently required and supported by the GPU, firmware package, and drm-kmod build. If intentionally enabling GuC submission, the loader setting is:

```sh
# /boot/loader.conf
hw.i915kms.enable_guc="1"
```

This is a separate driver configuration choice; it is not part of patch application or compilation. Reboot is required for a loader.conf change to take effect. Confirm firmware load and submission status in boot/kernel logs before treating it as active. Do not use this setting as a workaround for GEM page-retention failures or RCS0 hangs.

## Installation policy

This repository does not currently provide a package, installer, automatic kernel-module replacement, or supported binary distribution. Do not overwrite a working `i915kms.ko` without preserving the original file, verifying both hashes, preparing a tested recovery path, and planning a controlled reboot. Package upgrades may overwrite manually installed modules.

## License

Project-authored material in this repository is MIT-licensed. The patches modify an upstream i915 file that is also marked MIT; original upstream copyright and license notices remain applicable and must be preserved. See [LICENSE](LICENSE) for the license text and this section for provenance.

## References

- [FreeBSD drm-kmod](https://github.com/freebsd/drm-kmod)
- [FreeBSD Bugzilla 298279](https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=298279)
- [Investigation record](docs/INVESTIGATION.md)
