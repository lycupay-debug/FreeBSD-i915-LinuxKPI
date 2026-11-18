<!-- SPDX-License-Identifier: BSD-2-Clause -->
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

## Meteor Lake shmem-page restoration port (2026-11-17)

### Platform and reproduction evidence

- FreeBSD 15.2-BETA2, amd64 (`kern.osreldate=1502000`).
- Intel Core Ultra 5 125H / Meteor Lake-P integrated graphics, PCI ID `8086:7d55`.
- Installed baseline package: `drm-612-kmod-6.12.85.1502000_3`.
- GEM 4 MiB WB mmap test: initial write/read and pattern verification passed; `DROP_UNBOUND` was triggered; `WILLNEED retained=1`; after shrink the first 64-bit word was zero and the test reported `RESULT=DATA_LOSS`.
- No new GPU HANG was observed in the before/after dmesg window. The reproducer does not submit a GPU command buffer. This establishes the GEM backing-page data-loss regression on this MTL setup, not a causal explanation for RCS0 hangs.

### Port scope and source review

Added `patches/0002-i915-freebsd-gem-shmem-page-restore.patch`, derived from the existing Skylake patch and applied to the exact drm-kmod 6.12.85 source tree at:

`/opt/i915-mtl-upstream-audit-20261117/drm-kmod/source-6.12.85/drm-kmod-drm_v6.12.85_3`

The port removes only the `IS_SKYLAKE(i915)` hardware-family restriction and renames the helper to `i915_restore_shmem_pages`. It retains the shmem-backend, struct-page, WILLNEED, live-reference, and valid-pages guards. It does not extend to TTM or LMEM backends. The helper remains FreeBSD-only and runs after mmap-offset release but before the GEM backend drops the pages.

FreeBSD 15.2-BETA2 VM source review confirmed:

- `vm_page_tryxbusy()` returns 1 on success and 0 on failure; the helper's negated test handles failure.
- `vm_page_insert()` returns 0 on success and 1 when radix insertion fails; the helper treats nonzero as failure.
- `vm_page_insert_lookup()` clears the tentative object association if radix insertion fails.

The helper holds the shmem backing object's write lock, does not wait for page busy ownership, skips pages already resident at the expected index, refuses to overwrite another resident page, requires the page to be detached, reinserts it, marks it dirty, and releases exclusive busy ownership. If a later page cannot be restored, the function returns an error; already restored pages remain resident and are skipped on a subsequent attempt. Error-path and concurrency behavior still require runtime stress validation.

### Build evidence

- Patch application to a fresh copy of the exact 6.12.85 source succeeded (`patch -p1`, both hunks).
- Candidate source/build directory: `/opt/i915-mtl-upstream-audit-20261117/build/drm612-shmem-restore-candidate`.
- Build command: `make -j4 KMODS="dmabuf ttm drm i915"`.
- Build result: exit status 0; `i915kms.ko` linked successfully; no compiler warning/error lines were found in `/var/tmp/i915-shmem-build.log`.
- Candidate module: `.../build/drm612-shmem-restore-candidate/i915/i915kms.ko`.
- Candidate module SHA-256: `110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d`.
- `kldxref -d` successfully read the module metadata and reported expected dependencies, including kernel.1502000, drmn.2, ttm.1, linuxkpi.1, and dmabuf.1.
- Candidate module was not installed, loaded, or used to reboot the laptop. The `kldstat` observation was read-only and reflects the already-running system module, not this candidate.

### Validation boundary / next gate

Status: patch applies and candidate module builds; runtime regression is **not yet verified** on MTL. The next validation gate is a controlled A/B run of the existing `tests/run-gem-wb-shmem-test.sh` against the candidate module, with a known-good rollback path and explicit approval before module replacement/loading/reboot. Repeat cycles, multiple GEM objects, memory pressure, concurrent mmap/unmap, and error-path stress are also unverified. Do not describe this port as a proven fix until those tests pass.

### Repository state

- Repository: `/opt/FreeBSD-i915-LinuxKPI`.
- Branch: `fix/freebsd-mtl-shmem-page-restore`.
- Base commit: `26fffe3c38740a8e52768aa0bdfa7d37c139277d`.
- GitHub CLI OAuth login verified for `lycupay-debug`; HTTPS Git protocol selected.
- At the time of this record, the port patch and investigation update are local changes; commit and push are pending final diff review.

### Git publication (2026-11-17)

- Commit: `a68251e54cf7cea2905810a042a44f4aae4e72b9` (`Port GEM shmem page restoration to Meteor Lake`).
- Branch pushed successfully: `origin/fix/freebsd-mtl-shmem-page-restore`.
- Remote `git ls-remote` confirmed the branch points to the same commit.
- GitHub CLI OAuth status was verified for `lycupay-debug`; Git HTTPS credential integration was configured with `gh auth setup-git`.
- Pull request has not been created.
- The worktree was clean after push.

### Regression harness and rollback preparation (2026-11-17)

- Added `tests/Makefile`; `make -C tests clean all` succeeded on FreeBSD 15.2-BETA2 with `-Wall -Wextra -Werror` and system libdrm 2.4.134.
- Updated the C reproducer to take the debugfs node from `DROP_CACHES_NODE`, write exactly the two command bytes (`1\n`) to `DROP_UNBOUND`, close the GEM handle, and release mappings/file descriptors on all exits.
- Updated the runner with positive `ITERATIONS` validation, configurable debugfs node, on-disk module SHA-256 guard, metadata output, and an explicit warning that the on-disk module file hash does not prove the identity of the already-loaded kernel image.
- `sh -n tests/run-gem-wb-shmem-test.sh` passed.
- The runner's hash guard was tested with an intentionally incorrect expected hash; it rejected the current baseline module with exit status 2 before mounting debugfs or triggering shrink.
- The GEM regression itself was not run during this stage because it triggers global `DROP_UNBOUND` and the current system is an active desktop. The earlier baseline DATA_LOSS reproduction remains the evidence for the unpatched driver.
- Prepared, but did not execute, candidate and rollback artifacts under `/home/dll/Downloads/i915-mtl-shmem-restore-20261117/`. Original on-disk module SHA-256: `d8569101411a2e97ae18625fbe153a61d97d6f7a5968099b8cb38759d7faf8f5`. Candidate SHA-256: `110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d`.
- The install and rollback scripts require explicit confirmation environment variables, verify exact hashes, never reboot, and preserve a copy of the pre-swap on-disk module. Both scripts passed `sh -n`.
- Verified after preparation that `/boot/modules/i915kms.ko` still has the original hash and `kldstat` still reports the currently loaded i915kms module. No module was installed, loaded, unloaded, or rebooted.

### Current validation status

The patch source and module build are verified. The userland test harness now compiles cleanly and has guard-path validation. Candidate runtime behavior on Meteor Lake is **not yet tested**. A controlled test still requires explicit authorization to replace the on-disk module and reboot into it; keep the original module and recovery scripts available. A separate RCS0 hang investigation remains independent and unresolved.


---

## Runtime status update (2026-11-18)

This section supersedes earlier statements in this document that the Meteor Lake candidate had not been installed or loaded. Those statements accurately described the state on 2026-11-17, before the subsequent module installation and reboot.

### Read-only laptop inspection

On 2026-11-18, the maintainer's current laptop reported:

- OS: FreeBSD 15.2-BETA2, amd64.
- GPU: Intel Meteor Lake-P, PCI ID `8086:7d55`.
- `/boot/modules/i915kms.ko` SHA-256: `110d696361b2b476fbdfc8643968c7208c506250d609fe1e13c8a9438016bf0d`, matching the recorded candidate build.
- `kldstat` lists `i915kms.ko` loaded from `/boot/modules/i915kms.ko`.
- System boot time was later than the module file's modification time; `/etc/rc.conf` contains `kld_list="i915kms"`.
- `hw.i915kms.enable_guc="1"` is configured in `/boot/loader.conf`.

Together these observations strongly support that the candidate module was loaded at boot. They do not provide a cryptographic hash of the in-memory module image.

### What has and has not been validated

- **Verified:** the on-disk module hash matches the candidate build; the module is listed as loaded; the candidate source patch applies and the module build completed successfully in the recorded build environment.
- **Not yet verified:** a fresh GEM WB mmap / WILLNEED / DROP_UNBOUND regression run against the currently loaded Meteor Lake candidate.
- **Not yet verified:** repeated shrink cycles, memory-pressure stress, multiple objects, concurrent mmap/unmap, long-duration stability, and exceptional error paths.
- **Unresolved separately:** RCS0 GPU hangs. The GEM reproducer does not submit GPU command buffers and cannot establish an RCS0 fix.

A prior observation of no display corruption over approximately one day occurred with both the page-lifecycle patch and GuC 1 enabled. Because both were active together, no causal attribution can be made.

Do not describe the Meteor Lake patch as a proven fix until the targeted regression and further stability tests have been completed.
