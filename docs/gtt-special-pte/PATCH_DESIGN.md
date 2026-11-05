# Patch design: special-PTE insertion for i915 device PFNs

**Status:** candidate implementation for review. The patches have passed a source-context dry run only. They have not been compiled, installed, loaded, or runtime-tested.

## 1. Goals

1. Prevent i915 GTT/device PFNs from entering the ordinary LinuxKPI PFN-to-vm_page lookup path.
2. Preserve the Linux i915 contract: PTEs created by these callbacks are special mappings and are not associated with an ordinary struct page.
3. Provide a FreeBSD managed-device-pager backing-object entry without pretending the target physical PFN is ordinary RAM.
4. Give every fake page an explicit allocation, insertion, busy-lock, removal, and release lifecycle.
5. Keep the existing Skylake GEM backing-page patch series independent.

## 2. Non-goals

This patch series does not:

- Fix or claim to fix RCS0 GPU HANG / preemption timeout.
- Add NVDEC or change NVIDIA 930M capabilities.
- Change the active desktop GPU or Xorg configuration.
- Modify Firefox VA-API, FFmpeg settings, or the Intel i915 GEM shmem patch.
- Generalize the API to every DRM driver or every architecture without review.
- Replace all LinuxKPI ordinary PFN insertion callers.
- Claim stability based on a patch dry run.

## 3. Patch series

### Patch 1 — LinuxKPI and VM support

File: patches/gtt-special-pte/0001-linuxkpi-special-pte-api.patch

Changes:

| File | Responsibility |
|---|---|
| sys/compat/linuxkpi/common/include/linux/mm.h | Declare lkpi_vmf_insert_special_pte_locked() |
| sys/compat/linuxkpi/common/src/linux_page.c | Implement special-PTE insertion using a fake page in an OBJT_MGTDEVICE object |
| sys/vm/vm_page.h | Define VPO_FAKE_OWNED and declare vm_page_getfake_nowait() |
| sys/vm/vm_page.c | Share fake-page allocation logic between WAITOK and NOWAIT variants |
| sys/vm/device_pager.c | Release owned fake pages during managed-device pager page removal |
| sys/vm/vm_object.c | Release owned fake pages during generic object termination |

This is a FreeBSD base-system patch. It changes VM and LinuxKPI source, not only the external i915 module.

### Patch 2 — i915 callers

File: patches/gtt-special-pte/0002-i915-use-special-pte.patch

Changes the FreeBSD branches of both remap_pfn() and remap_sg() in:

    drivers/gpu/drm/i915/i915_mm.c

from lkpi_vmf_insert_pfn_prot_locked() to lkpi_vmf_insert_special_pte_locked().

The remap_pfn() change addresses the observed GTT path. The remap_sg() change aligns the second Linux pte_mkspecial() caller with the same FreeBSD semantics. It is not asserted to be part of the observed crash stack.

## 4. API behavior

The ordinary API remains available:

    lkpi_vmf_insert_pfn_prot_locked()

The new API is explicit:

    lkpi_vmf_insert_special_pte_locked()

It requires the VMA backing object to be OBJT_MGTDEVICE and the caller to hold that object's write lock.

The new API:

1. Rejects an address outside [vm_start, vm_end) with VM_FAULT_SIGBUS.
2. Initializes a pctrie iterator for the VMA object.
3. Computes the page index from addr - vm_start.
4. Looks for an already resident page at that object index.
5. If absent, allocates a fictitious page with the requested PFN and cache mode.
6. Marks the page VPO_FAKE_OWNED so the VM cleanup paths know that this object owns the fake allocation.
7. Inserts it into the managed-device object.
8. Marks it valid and deliberately retains the allocation-time exclusive busy lock. The managed-device pager populate contract requires the callback to return with populated pages busy.
9. If a page already exists at that index, vm_page_grab_iter() returns it busy; the function preserves the existing entry and updates its memory attribute.
10. Increments vm_pfn_count and returns VM_FAULT_NOPAGE with the page still busy.

The function never calls PHYS_TO_VM_PAGE() for the requested special PFN.

## 5. Why a fake page is needed

The special PTE must not create ordinary PV tracking for the target physical address. At the same time, the FreeBSD managed device pager stores resident entries in a vm_object and its page-management code operates on vm_page entries.

The fake page is an object bookkeeping entry:

- PG_FICTITIOUS identifies it as fictitious.
- phys_addr stores the physical PFN being represented.
- VPO_UNMANAGED means it is not PV-managed.
- VPO_FAKE_OWNED distinguishes fake pages allocated by this new special-PTE API from other fictitious pages.
- The actual PTE remains a special mapping and does not gain normal PV tracking for the device PFN.

This distinction is essential: the fake vm_page is not a claim that the GMADR address is system RAM.

## 6. Allocation and ownership state machine

### 6.1 Successful insertion

    vm_page_getfake_nowait()
        |
        v
    fake page allocated and exclusively busy
        |
        v
    set VPO_FAKE_OWNED
        |
        v
    vm_page_iter_insert()
        |
        v
    vm_page_valid()
        |
        v
    return to linux_cdev_pager_populate with exclusive busy held
        |
        v
    normal VM fault completion releases busy ownership

Do not call vm_page_xunbusy() after successful insertion. vm_page_initfake() creates the fake page with the current thread holding exclusive busy ownership, and vm_page_iter_insert() preserves it. linux_cdev_pager_populate() requires the fault callback to return with every populated page busy; the normal VM fault completion path releases that ownership later.

### 6.2 Insertion failure

If vm_page_iter_insert() fails, the page was not published into the object. It remains exclusively busy and is returned directly through vm_page_putfake().

### 6.3 Managed-device pager unmap

The local path is:

    lkpi_unmap_mapping_range()
      cdev_pager_lookup()
      cdev_mgtdev_pager_free_pages()
        vm_page_tryxbusy()
        cdev_mgtdev_pager_free_page()
          pmap_remove_all()
          vm_page_iter_remove()
          [vm_page_iter_remove releases busy]
          vm_page_tryxbusy()
          vm_page_putfake()

The second busy acquisition is necessary because the local vm_page_iter_remove() implementation releases the busy lock before returning.

### 6.4 Generic VM object termination

The local vm_object_terminate_single_page() callback begins with vm_page_assert_unbusied(). For a page marked VPO_FAKE_OWNED, the candidate:

1. Confirms PG_FICTITIOUS.
2. Acquires exclusive busy ownership.
3. Calls vm_page_putfake().
4. Does not route the fake page through ordinary vm_page_drop()/vm_page_free().

The busy acquisition is required by the local vm_page_putfake() contract. The lock ordering and failure assumptions must be reviewed before production use.

## 7. Locking and ownership table

| Operation | Page state on entry | Operation | State on exit |
|---|---|---|---|
| vm_page_getfake_nowait | not allocated | allocate + initfake | exclusively busy |
| successful special insert | exclusively busy | insert and validate; retain busy for pager contract | resident, exclusively busy |
| failed special insert | exclusively busy | vm_page_putfake | returned to UMA zone |
| managed pager removal | exclusively busy from busy_acquire | iter_remove | removed, unbusied |
| fake page release after pager removal | unbusied | busy_acquire + putfake | returned to UMA zone |
| generic object termination | unbusied | busy_acquire + putfake | returned to UMA zone |

The table reflects the current FreeBSD 15.1 source inspected for this candidate. Any upstream change to vm_page_getfake(), vm_page_iter_insert(), vm_page_iter_remove(), vm_page_putfake(), or object termination requires re-review.

## 8. Error handling

- Invalid virtual address: VM_FAULT_SIGBUS.
- Fake-page allocation failure: VM_FAULT_OOM.
- Object insertion failure: release the uninserted fake page and return VM_FAULT_OOM.
- The i915 caller maps VM_FAULT_OOM to -ENOMEM and VM_FAULT_ERROR to -EFAULT.
- No fallback to lkpi_vmf_insert_pfn_prot_locked() is permitted. Such a fallback would reintroduce the invalid PFN-to-vm_page path.

The cleanup path for partially inserted mappings must be checked against zap_vma_ptes() and the managed-device pager lifecycle during build/review.

## 9. Review questions before kernel build

1. Is OBJT_MGTDEVICE guaranteed for every VMA reaching both i915 remap callbacks?
2. Does the first-insertion-wins behavior remain correct if the same object index is reached with a different PFN?
3. Are all callers serialized by the VMA object write lock as required?
4. Can the fake page become reachable by any consumer that expects a normal managed page?
5. Is the additional busy acquisition in the pager-removal path safe under all object/page lock interleavings?
6. Is the busy acquisition in generic object termination guaranteed to succeed after vm_page_assert_unbusied() under the object's dead/termination state?
7. Does pmap_remove_all() plus the object/pager teardown guarantee no PTE can retain the PFN after fake-page release?
8. Does remap_sg() always use the managed-device object type and the same teardown protocol?
9. Are cache attributes consistent when an object index is reused?
10. Does error unwinding correctly remove all inserted fake entries and release every owned fake page?
11. Are VPO_FAKE_OWNED flag allocation and future FreeBSD VM flag changes conflict-free?
12. Does the patch compile with both INVARIANTS and non-INVARIANTS kernels?

These questions are not marked resolved merely because the patch applies cleanly.

## 10. Why this patch touches both LinuxKPI and i915

The interface and implementation live in LinuxKPI/FreeBSD VM because FreeBSD needs a new representation and lifecycle for special device PFNs. The i915 module is the caller that must select the correct API.

Therefore:

- The special-PTE implementation is not solely an i915 module patch.
- The caller change is not sufficient without the base-system API and fake-page lifecycle.
- The LinuxKPI/VM patch alone does not automatically fix i915 until the i915 call sites are switched.
- A new kernel/base-system build and a matching drm-kmod/i915 build are expected if this design is retained.

The exact build/deployment split must be finalized after the patch is reviewed against the target source trees.

## 11. Why the object pointer is cleared before tryxbusy

The local vm_page_tryxbusy() implementation first acquires the page busy lock, then checks whether the page still has an object whose lock is currently held. If so, it releases the page busy lock and reports failure. This avoids acquiring a page busy lock while the caller already holds the owning object lock.

Both fake-page release paths detach the page from its object before attempting tryxbusy:

- vm_page_iter_remove() removes the page from the object and clears its object association before returning.
- vm_object_terminate_single_page() clears p->object before the VPO_FAKE_OWNED branch.

At those points the page is no longer reachable through the object's resident-page index. Clearing the object association is therefore part of the lock protocol, not a cosmetic assignment. It allows the non-sleeping tryxbusy operation to claim the detached fake page without seeing the current thread's object write lock.

This ordering is sensitive to FreeBSD VM internals. It must be revisited if vm_page_iter_remove(), vm_page_tryxbusy(), or object termination semantics change.
