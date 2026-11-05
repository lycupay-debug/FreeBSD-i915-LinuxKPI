# Validation record and plan

**Current status:** source-context dry run passed. No build, installation, module load, GPU test, or runtime validation has been performed for this patch series.

## 1. Baseline used for patch context

| Component | Observed target |
|---|---|
| OS | FreeBSD 15.1-RELEASE, amd64 |
| Kernel version | GENERIC; kern.osreldate 1501000 |
| Base source | /usr/src |
| DRM package | drm-612-kmod 6.12.85.1501000_2 |
| DRM source tree used for caller context | /usr/local/src/i915-gem-patch/exact-6.12.85_3/drm-kmod-drm_v6.12.85_3 |
| GPU | Intel Skylake-U HD Graphics 520, PCI 8086:1916 |
| LinuxKPI source | /usr/src/sys/compat/linuxkpi |
| VM source | /usr/src/sys/vm |

The installed package metadata reported drm port git hash 46cf3d9696491c61d91ecd22db11c7f7c1e62c24. The exact source tree used for the i915 hunk is retained at the path above. Before a build, verify that the source tree matches the package/build provenance and has not changed.

## 2. Completed checks

| Check | Result | Scope |
|---|---|---|
| Read-only source inspection of LinuxKPI ordinary PFN insertion | Completed | Identified PHYS_TO_VM_PAGE() and busy-lock path |
| Read-only source inspection of dense PHYS_TO_VM_PAGE() | Completed | Confirmed dense-array slot return behavior |
| Read-only inspection of vm_page_getfake()/putfake() | Completed | Confirmed fake-page busy-lock preconditions |
| Read-only inspection of vm_page_iter_insert()/remove() | Completed | Confirmed insertion does not unbusy; removal does unbusy |
| Download and inspection of D59883 prototype diff | Completed | Review status still Needs Review |
| Patch 1 context dry run | Passed | Temporary copies of exact local FreeBSD 15.1 files |
| Patch 2 context dry run | Passed | Temporary copy of exact local drm-kmod i915_mm.c |
| Kernel/base-system build | Not run | No compile claim |
| drm-kmod/i915 module build | Not run | No compile claim |
| Module installation/load | Not run | No deployment |
| Runtime GTT/VA-API test | Not run | Intentionally blocked pending review |
| Long-duration stability test | Not run | Not established |

The dry-run command used temporary copies of the source files and did not modify /usr/src, the installed modules, loader configuration, or the running kernel.

## 3. Required static review before any build

- Review every changed hunk against the exact FreeBSD 15.1 source.
- Verify all fake-page ownership paths, including insertion failure, explicit unmap, object termination, and pager teardown.
- Verify all callers of lkpi_vmf_insert_special_pte_locked().
- Verify that remap_pfn() and remap_sg() always operate on OBJT_MGTDEVICE objects.
- Check the object/page lock order and failure assumptions around vm_page_tryxbusy().
- Check PTE removal ordering and ensure the physical PFN cannot be reused while a special mapping remains.
- Check VPO_FAKE_OWNED does not collide with any flag introduced by the target source revision.
- Check error unwinding for partial page insertion.
- Review the upstream D59883 status again; do not imply it was accepted or merged.

## 4. Proposed build sequence — not yet authorized or executed

The patch changes FreeBSD base VM and LinuxKPI files as well as drm-kmod i915. A module-only build is not sufficient if the LinuxKPI/VM API is changed in the kernel.

A future isolated build should:

1. Use the pre-test ZFS snapshot or a separate build environment.
2. Preserve the exact source tree and kernel configuration.
3. Apply patch 1 to the FreeBSD source tree.
4. Build the matching kernel/base modules and record the exact command, exit status, warnings, and artifact hashes.
5. Apply patch 2 to the matching drm-kmod source tree.
6. Build the matching i915 module against the patched base source and record command, exit status, warnings, and artifact hashes.
7. Inspect the generated symbols and module dependencies.
8. Do not install or load the candidate automatically.
9. Review the complete build log and patch diff before deployment.

The exact FreeBSD build target and install paths must be determined from the target machine's current source/configuration at that time. Do not assume that rebuilding only i915kms.ko is sufficient.

## 5. Proposed runtime validation — high risk, not yet approved

The previous FFmpeg hwupload test caused two kernel panics. It must not be repeated on the current active installation merely to see whether the candidate patch works.

Before any runtime test:

- Complete static review of the patch and lifecycle.
- Build in a separate environment if possible.
- Capture and verify the recovery snapshot identity.
- Keep original kernel and module artifacts and checksums.
- Ensure console/recovery access.
- Start with a narrowly scoped, low-risk test designed specifically for special-PTE insertion/removal.
- Run one controlled test at a time.
- Preserve kernel messages, vmcore/core.txt, test output, and artifact hashes.
- Stop immediately on a GPU hang, page fault, lock assertion, or unexpected display behavior.

A passing short test is only a single observation. Long-duration desktop stability, memory pressure, repeated mapping/unmapping, concurrent mappings, process exit, and error paths require separate validation.

## 6. Evidence recording format

For every future test, record:

- Date/time and timezone.
- Exact hardware and OS version.
- Kernel source revision and kernel configuration hash.
- drm-kmod source revision and package provenance.
- Patch series commit and SHA-256.
- Exact command, not a paraphrase.
- Exit status and complete relevant output.
- Whether a reboot occurred and who initiated it.
- Loaded module paths and hashes.
- Kernel log range before and after.
- Any vmcore/core.txt path and SHA-256.
- Result status: passed, failed, inconclusive, not run.
- Remaining limitations.

Do not label a build successful if only the patch dry run passed. Do not label the patch stable based on one successful runtime test.

## 7. Patch preparation update — 2026-11-05

Additional source-only checks completed after the initial draft:

- Added tests/gtt-special-pte/check-patches.sh. It runs patch --dry-run against the supplied FreeBSD and drm-kmod source roots and does not modify them.
- Executed the script with FREEBSD_SRC defaulting to /usr/src and DRM_SRC set to /usr/local/src/i915-gem-patch/exact-6.12.85_3/drm-kmod-drm_v6.12.85_3. Both patch contexts passed.
- Applied both patches to temporary copies under /tmp and inspected the resulting LinuxKPI, VM, and i915 call sites. The live /usr/src tree, live drm-kmod source tree, installed modules, and running kernel were not modified.
- Checked the target FreeBSD 15.1 VM source for VPO_* definitions using value 0x10 in the inspected VM/LinuxKPI directories; no collision was found.
- Reviewed vm_page_tryxbusy(): it is non-sleeping and returns 1 when exclusive busy acquisition succeeds. The candidate uses it after pager/object removal rather than a potentially sleeping busy-acquire path.

These checks improve confidence in patch context and the intended lock-state transitions. They do not replace a compile, INVARIANTS validation, runtime testing, or upstream review.
