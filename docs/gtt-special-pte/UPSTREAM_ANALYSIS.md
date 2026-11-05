# Upstream Linux and FreeBSD analysis

**Research date:** 2026-11-05
**Status:** source review complete for the identified API and lifecycle paths; upstream prototype is not an accepted/merged fix.

## 1. Linux mapping contract

The i915 source contains two related callbacks in i915_mm.c:

- remap_pfn(), used by remap_io_mapping() for the GTT I/O mapping path involved in this incident.
- remap_sg(), used by remap_io_sg() for scatter-gather I/O mappings.

On Linux, both callbacks construct their PTE with pte_mkspecial(pfn_pte(...)). The nearby source comment states:

    /* Special PTE are not associated with any struct page */

A special PTE represents a PFN mapping without treating the target physical address as an ordinary Linux struct page. This is the relevant semantic distinction.

The FreeBSD branch in the local drm-kmod source called lkpi_vmf_insert_pfn_prot_locked() in both callbacks. That function is designed for the ordinary PFN insertion model and attempts to find or construct a vm_page association. The two APIs are not interchangeable.

## 2. D59883: special-PTE LinuxKPI prototype

Reference: https://reviews.freebsd.org/D59883

Title: linuxkpi: lkpi_vmf_insert_special_pte_locked()

Author shown by the review: kib. The review page was marked **Needs Review** when checked on 2026-11-05. The review reports lint skipped and unit tests skipped. The review discussion explicitly describes the change as a prototype; the author stated that only the LinuxKPI portions had been compiled at that point.

The downloadable review diff was inspected. It changes:

- sys/compat/linuxkpi/common/include/linux/mm.h
- sys/compat/linuxkpi/common/src/linux_page.c
- sys/vm/device_pager.c
- sys/vm/vm_object.c
- sys/vm/vm_page.h
- sys/vm/vm_page.c

The prototype's core design is appropriate to the incident:

1. Add lkpi_vmf_insert_special_pte_locked().
2. Require a managed device pager backing object (OBJT_MGTDEVICE).
3. Allocate a fictitious vm_page for the requested PFN.
4. Insert that fake page into the VMA's backing object as bookkeeping.
5. Keep the mapping from creating ordinary PV tracking for the target physical PFN.
6. Release the fake page when the managed device pager removes it or when its object is terminated.
7. Change the i915 remap_pfn() caller to use the special-PTE API.

The local candidate preserves this architecture but includes additional busy-lock lifecycle corrections described below.

## 3. Lifecycle defects identified while reviewing the prototype

The D59883 diff is a prototype, not a ready-to-apply production patch. Comparing it with the actual FreeBSD 15.1 implementation exposed busy-lock transitions that must be handled.

### 3.1 Newly allocated fake page must remain busy after insertion

Local FreeBSD source facts:

- vm_page_getfake() calls vm_page_initfake().
- vm_page_initfake() initializes busy_lock to VPB_CURTHREAD_EXCLUSIVE.
- vm_page_iter_insert() inserts the page into the object/radix tree and does not release the page busy lock.
- linux_cdev_pager_populate() requires a successful fault callback to return with every populated page busy. Existing-page handling through vm_page_grab_iter() preserves that busy state.

The first candidate draft incorrectly called vm_page_xunbusy() after successful insertion. That would violate the pager populate contract for newly allocated pages while the already-resident path returned a busy page.

The corrected candidate deliberately keeps the exclusive busy state after vm_page_valid() and returns it to the pager. The normal VM fault completion path owns the subsequent unbusy transition.

On insertion failure, the page was never published into the object and remains exclusively busy, so vm_page_putfake() can release it directly.

### 3.2 Managed-device pager removal releases busy before putfake

The local FreeBSD 15.1 vm_page_iter_remove() implementation performs:

1. Remove the page from the radix iterator.
2. Remove the page/object association.
3. Drop the object reference.
4. Call vm_page_xunbusy().

The D59883 prototype adds vm_page_putfake() immediately after vm_page_iter_remove() in cdev_mgtdev_pager_free_page(). However, vm_page_putfake() asserts that the page is exclusively busy.

The candidate patch therefore reacquires exclusive busy ownership with the non-sleeping vm_page_tryxbusy() after vm_page_iter_remove() and before vm_page_putfake(). Failure is treated as an invariant violation and panics rather than freeing a page without ownership.

This transition must be reviewed carefully under the actual VM lock-order rules.

### 3.3 Generic object termination enters with the page unbusied

The local vm_object_terminate_single_page() begins with vm_page_assert_unbusied(p). The D59883 prototype adds a direct vm_page_putfake(p) branch there.

The local vm_page_putfake() requires exclusive busy ownership. Calling it directly from this callback would violate the function's precondition.

The candidate patch reacquires the exclusive busy state before calling vm_page_putfake(). This path also needs review for the object-termination lock order and the guarantee that the page cannot be concurrently re-referenced.

### 3.4 Why these are not cosmetic changes

vm_page_putfake() is not a generic free operation. It asserts that the page is fictitious, unmanaged, and exclusively busy, then marks its busy lock freed and returns it to the fake-page UMA zone.

The three paths above must agree on the same ownership protocol. A mismatch can cause an INVARIANTS assertion, a permanently busy page, a use-after-free, or a double release.

The candidate's busy transitions are explicit and documented. They have not yet been compiled or exercised.

## 4. D59506: detect uninitialized pages

Reference: https://reviews.freebsd.org/D59506

Title: lkpi_vmf_insert_pfn_prot_locked(): detect uninitialized pages

The review proposes detection of uninitialized vm_page slots in the ordinary PFN insertion path. It is relevant because it discusses invalid vm_page descriptors and DRM 6.12 behavior.

It is not a substitute for special-PTE semantics:

- The incident PFN is an I/O aperture PFN.
- Linux i915 explicitly uses pte_mkspecial() for this mapping.
- The ordinary API still models the PFN as a vm_page.
- Detecting or rejecting an invalid slot may turn a panic into a fault, but it does not implement the intended special mapping.

The candidate therefore does not use D59506 as the primary fix.

## 5. D53412: NULL page lookup handling

Reference: https://reviews.freebsd.org/D53412

The stable/15 change from December 2025 added graceful handling when PHYS_TO_VM_PAGE() returns NULL in lkpi_vmf_insert_pfn_prot_locked().

The local FreeBSD 15.1 source already has a NULL check. The incident returned a non-NULL pointer into vm_page_array, but the pointed-to descriptor was uninitialized/invalid.

Therefore D53412 addresses a different failure condition and does not resolve this incident.

## 6. D58320: Linux struct page transition

Reference: https://reviews.freebsd.org/D58320

D58320 concerns the transition from the historical Linux struct page == FreeBSD vm_page alias toward a separate LinuxKPI struct page representation and a shadow mapping.

The review page showed the change as accepted, with an MFC plan. The local FreeBSD 15.1 source inspected for this work did not contain PAGE_IS_LKPI_PAGE markers or the new special-PTE API.

The candidate patch is written against the actual local 15.1 source, not against a presumed newer source tree. If the base system's LinuxKPI page representation changes later, this patch must be rebased and re-reviewed.

## 7. Why fictitious-range registration is not the selected fix

FreeBSD supports fictitious pages and fictitious physical ranges for special address ranges. However, the local dense PHYS_TO_VM_PAGE() implementation first returns a dense vm_page_array slot whenever the PFN index falls inside the array's numeric bounds. Only otherwise does it query the fictitious-page lookup.

Registering a fictitious range does not prevent the dense-array branch from returning an invalid slot first.

More importantly, a special PTE is not intended to create a normal managed-page/PV association with the target PFN. The fake-page object entry is bookkeeping for the managed device pager; its phys_addr records the PFN, while the mapping itself retains special-PTE semantics.

## 8. Caller scope: remap_pfn and remap_sg

The panic stack directly implicates remap_io_mapping() -> remap_pfn(). That is the minimum incident reproducer path.

The local i915 source also uses Linux pte_mkspecial() in remap_sg(), while its FreeBSD branch calls the ordinary PFN API. The candidate changes both FreeBSD call sites to the special-PTE API so the FreeBSD branch matches the Linux mapping contract for both callbacks.

This is a deliberate semantic parity change, not a claim that remap_sg() caused the observed panic. It must be validated against the VMA object type and lifetime assumptions for all relevant remap_io_sg() callers.

## 9. References and status snapshot

| Reference | Subject | Status observed |
|---|---|---|
| D59883 | Add LinuxKPI special-PTE insertion API and fake-page lifecycle | Needs Review; prototype; lint/unit skipped |
| D59506 | Detect uninitialized pages in ordinary PFN insertion | Needs Review; not a special-PTE replacement |
| D53412 | Handle NULL page lookup | Committed to stable branches; does not cover non-NULL invalid dense slot |
| D58320 | Separate Linux struct page from vm_page | Accepted review; local 15.1 tree does not contain its markers |

Review pages can change. This table records the status visible during the 2026-11-05 investigation and should be refreshed before rebasing or publishing a later release.
