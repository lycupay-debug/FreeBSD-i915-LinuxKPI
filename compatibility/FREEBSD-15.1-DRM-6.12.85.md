# Compatibility record: FreeBSD 15.1 and drm-kmod 6.12.85

**Record date:** 2026-11-05
**Status:** candidate patch context target; no runtime compatibility claim.

## Target machine

- FreeBSD 15.1-RELEASE, amd64
- Kernel reported GENERIC, kern.osreldate 1501000
- Intel Skylake-U HD Graphics 520, PCI 8086:1916
- NVIDIA GeForce 930M is also present, but it is not implicated by the panic stacks documented in this investigation.

## Installed DRM package metadata

- Package: drm-612-kmod
- Version: 6.12.85.1501000_2
- Origin: graphics/drm-612-kmod
- FreeBSD version annotation: 1501000
- Package port git hash: 46cf3d9696491c61d91ecd22db11c7f7c1e62c24
- Package repository annotation: USTC-ports

The drm-kmod metaport was also installed. The actual module package is drm-612-kmod.

## Source trees inspected

FreeBSD base and LinuxKPI:

    /usr/src/sys/compat/linuxkpi/common/include/linux/mm.h
    /usr/src/sys/compat/linuxkpi/common/src/linux_page.c
    /usr/src/sys/vm/device_pager.c
    /usr/src/sys/vm/vm_object.c
    /usr/src/sys/vm/vm_page.h
    /usr/src/sys/vm/vm_page.c

i915 source used for the caller patch:

    /usr/local/src/i915-gem-patch/exact-6.12.85_3/drm-kmod-drm_v6.12.85_3/drivers/gpu/drm/i915/i915_mm.c

The exact i915 source tree was used for a patch-context dry run. Confirm its provenance and cleanliness again before any future build.

## Important source facts

- The local LinuxKPI header does not declare lkpi_vmf_insert_special_pte_locked().
- The local LinuxKPI implementation's ordinary PFN path calls PHYS_TO_VM_PAGE().
- The local dense PHYS_TO_VM_PAGE() implementation can return a dense-array slot based on numerical range.
- The local vm_page_getfake() initializes a fake page as exclusively busy.
- The local vm_page_iter_insert() does not release that busy state.
- The local vm_page_iter_remove() releases the busy state.
- The local vm_page_putfake() requires exclusive busy ownership.
- The local Linux i915 remap_pfn() and remap_sg() paths use pte_mkspecial().
- The local FreeBSD i915 branches used the ordinary LinuxKPI PFN insertion API.

These facts are the reason the candidate patch includes explicit busy-lock handling and changes both i915 callbacks.

## Compatibility limits

The patch is scoped to the inspected FreeBSD 15.1 source and drm-kmod 6.12.85 source. It is not declared compatible with:

- FreeBSD 14.x or 16.x.
- FreeBSD CURRENT or later stable/15 revisions.
- Other drm-kmod versions or Linux DRM versions.
- Other architectures or GPUs.
- Other LinuxKPI special-PFN callers.

A source update can change VM object lifecycle, fictitious-page handling, page flags, iterator semantics, or lock behavior. Rebase and review are mandatory after any such change.
