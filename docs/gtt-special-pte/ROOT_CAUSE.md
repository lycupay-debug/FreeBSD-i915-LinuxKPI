# Root-cause investigation: i915 GTT I/O PFN entered the ordinary LinuxKPI page path

**Status:** high-confidence source-level root-cause analysis; patch not yet compiled or runtime-validated.

**Incident date:** 2026-11-05
**Target platform:** FreeBSD 15.1-RELEASE-p6, amd64; Intel Skylake-U HD Graphics 520 (PCI 8086:1916); drm-kmod 6.12.85.

## 1. Executive summary

Two kernel panics occurred while an assistant-started FFmpeg VA-API test was running. Both vmcores show the current process as an FFmpeg video-filter worker and both stacks enter the Intel i915 GTT fault path:

    vm_page_busy_acquire
      lkpi_vmf_insert_pfn_prot_locked
        remap_io_mapping                 [i915kms.ko]
          vm_fault_gtt                   [i915kms.ko]
            linux_cdev_pager_populate
              vm_fault

The faulting PFN is not ordinary system RAM. It is an Intel GPU Graphics Memory Address Remapping Table (GMADR) aperture address. The FreeBSD LinuxKPI implementation passed that PFN to PHYS_TO_VM_PAGE(). On this machine, the dense physical-page implementation can return an address inside vm_page_array for any PFN whose index falls inside the array's numerical range. That does not prove that the slot describes real RAM.

Offline KGDB analysis identified the returned slot as an uninitialized/invalid vm_page descriptor. LinuxKPI then treated that descriptor as a real page and attempted to acquire its busy lock. The corrupted object pointer in the descriptor led to the two panics.

**Current root-cause conclusion:** the i915 GTT device-I/O PFN was routed through an ordinary PFN-to-vm_page insertion API. That API assumes the PFN can be represented by a valid managed or registered fictitious vm_page. The Linux special-PTE mapping contract is different: the mapping is not associated with a normal struct page. The FreeBSD port needs a distinct special-PTE path with explicit fake-page bookkeeping and lifetime handling.

This is a source-level conclusion supported by both vmcores and the local source. It is not yet proof that every possible i915 special mapping or every FreeBSD platform has the same failure.

## 2. Incident timeline and evidence

System logs are in UTC. Shanghai time is UTC+8.

| Event | UTC | Shanghai |
|---|---:|---:|
| Panic / vmcore.1 | 2026-11-05 04:10:21 | 12:10:21 |
| Panic / vmcore.2 | 2026-11-05 04:13:38 | 12:13:38 |

The two sessions ended in crashes, not orderly shutdowns. savecore preserved two vmcores and their matching core.txt files.

### 2.1 Preserved dump hashes

| Artifact | Size | SHA-256 |
|---|---:|---|
| /var/crash/vmcore.1 | 1,669,079,040 bytes | 2f8e95eb30a593d29299ce2273f58509affa55f1738d73d507725ab0c99305e4 |
| /var/crash/core.txt.1 | — | 4c061c794db3d3cdd8392c1cebbb4267ca8c7cd79319db272d1a32c3333b639f |
| /var/crash/vmcore.2 | 900,603,904 bytes | 910f5ffe36ebd0b242fef5350c9766af6b868ab394e9e95ed866d8fe33c035c1 |
| /var/crash/core.txt.2 | — | 31d6d11cd7ef39ecb51f73716687ac43b82b6a7f1d432dd778daa01a09c65094 |

The original dump files were retained. They must not be deleted, overwritten, or treated as disposable test artifacts.

### 2.2 Common stack

Both dumps have substantially the same stack:

    vm_page_busy_acquire
      lkpi_vmf_insert_pfn_prot_locked
        remap_io_mapping
          vm_fault_gtt
            linux_cdev_pager_populate
              vm_fault

The first panic was a page fault (trap 12). The second was a general protection fault (trap 9). The active process in both cases was an FFmpeg process, with the current thread identified as a video-filter worker (vf#0:0).

### 2.3 Assistant-started test association

Immediately before the panics, the assistant initiated FFmpeg VA-API hwupload tests using a synthetic test source and the filter chain:

    testsrc2 -> format=nv12 -> hwupload

The test process was the current process in both vmcores. Both stacks entered the i915 GTT mapping path. This is a strong causal association between the assistant-started test and the panic path.

The assistant initiated the test. The incident must not be attributed to user action.

No further FFmpeg hwupload, VA-API render-node stress, hardware decode/encode, or Firefox VA-API tests should be run on the active installation until the source patch is reviewed and a separate validation plan is approved.

## 3. Physical-address evidence

### 3.1 Intel GMADR aperture

The Intel GPU PCI BAR range observed on this system was:

    0xb0000000 - 0xbfffffff

This is the GPU GMADR / GTT I/O aperture range, not a declaration that the entire range is backed by ordinary host RAM.

### 3.2 PFN and vm_page_array slot calculation

Offline KGDB inspection found the invalid descriptor pointer:

    0xfffffe0003c95df0

The local FreeBSD kernel reported:

- sizeof(struct vm_page) = 88 bytes
- vm_page_array index = 721914
- first_page = 16

Therefore:

    PFN = index + first_page
        = 721914 + 16
        = 721930
        = 0xb040a

    Physical address = PFN * PAGE_SIZE
                     = 0xb040a000

The address 0xb040a000 lies inside the Intel GMADR BAR range 0xb0000000-0xbfffffff.

The descriptor address is therefore a dense-array slot selected by the numerical PFN range, not evidence of a valid RAM page.

### 3.3 Descriptor contents

The vm_page_array slot at index 721914 contained values inconsistent with a valid initialized page. In vmcore.1:

| Field | Observed value |
|---|---|
| object | 0x200000000080 |
| pindex | 720577933245944565 |
| phys_addr | 0x200871900400000 |
| ref_count | 128 |
| busy_lock | 8192 |
| flags | 0 |
| oflags | 6 |

The phys_addr field does not equal the PFN-derived address 0xb040a000. The object and pindex values are also nonsensical for the intended page.

The same vm_page_array slot was inspected in vmcore.2 and contained different invalid-looking data. That is consistent with the slot being uninitialized memory rather than a stable page descriptor.

## 4. Source-level failure path

### 4.1 i915 GTT fault path

The i915 GTT fault handler reaches the remap_io_mapping path. In the FreeBSD drm-kmod source, remap_io_mapping uses remap_pfn as its per-page insertion callback.

The Linux implementation writes a PTE using:

    pte_mkspecial(pfn_pte(r->pfn, r->prot))

The Linux comment explicitly says that special PTEs are not associated with a struct page.

The FreeBSD branch in the local drm-kmod source instead called:

    lkpi_vmf_insert_pfn_prot_locked(r->vma, addr, r->pfn, r->prot)

That is the semantic mismatch under investigation.

### 4.2 LinuxKPI ordinary PFN insertion

The local FreeBSD 15.1 LinuxKPI implementation follows this sequence when the VMA object does not already contain a page at the target index:

1. Call PHYS_TO_VM_PAGE(IDX_TO_OFF(pfn)).
2. If a page pointer is returned, pass it to vm_page_busy_acquire().
3. Inspect page->object and potentially rename or insert the page into the VMA backing object.
4. Continue as though the PFN has a valid vm_page representation.

A NULL check exists in the local implementation. That protects against a PFN for which PHYS_TO_VM_PAGE() returns NULL. It does not protect against a non-NULL pointer to an uninitialized dense-array slot.

### 4.3 Dense PHYS_TO_VM_PAGE behavior

For VM_PHYSSEG_DENSE, the local FreeBSD implementation calculates the page index from the physical address. If the index is within the numerical bounds of vm_page_array, it returns:

    &vm_page_array[pi - first_page]

This branch does not independently establish that the slot corresponds to populated RAM or a valid initialized page.

Only when the PFN falls outside that dense array interval does the function consult the fictitious-page lookup.

Consequently, an I/O aperture PFN can numerically alias an in-range dense-array slot and produce a non-NULL but invalid vm_page pointer.

### 4.4 Panic mechanism

vm_page_busy_acquire() expects a valid vm_page descriptor. It reads fields such as page->object and uses the page busy lock.

In both dumps, the descriptor's object pointer was invalid. The panic occurred while the kernel attempted to use this invalid descriptor as a real page.

The panic is therefore not explained by a missing NULL check alone. The invalid pointer was non-NULL.

## 5. Competing explanations and exclusions

### NVIDIA

The second dump contains an NVIDIA DRM log message about a malformed PRIME fence ioctl. That message is not in the panic stack. Neither panic stack enters the NVIDIA module. It is not evidence that NVIDIA caused this failure.

The current evidence points to the i915/LinuxKPI VM insertion path.

### OOM or swap exhaustion

The available logs and dump summaries did not show OOM or swap exhaustion as the cause of either panic.

### Previous Skylake GEM backing-page patch

The earlier GEM patch modifies i915 GEM backing-page ownership/restoration in i915_gem_pages.c. It does not modify i915_mm.c remap_io_mapping or LinuxKPI's PFN insertion implementation. Its helper does not appear in either panic stack.

The two issues may both involve FreeBSD DRM/LinuxKPI compatibility, but they are distinct mechanisms. There is no direct evidence that the earlier GEM patch caused these panics. An indirect interaction has not been completely excluded.

### Existing RCS0 GPU HANG history

Earlier logs contain i915 RCS0 preemption-timeout/GPU-HANG messages. Those events may represent a separate i915 stability issue. The present panic stack is a GTT PFN/page-descriptor problem; it does not prove that the RCS0 hang and the GTT panic share one root cause.

## 6. Root-cause confidence and remaining uncertainty

| Statement | Evidence status |
|---|---|
| Both panics occurred during assistant-started FFmpeg hwupload tests | Directly supported by process identity, timing, and test history |
| Both stacks entered i915 GTT remap and LinuxKPI PFN insertion | Directly supported by vmcores |
| Faulting PFN maps into the Intel GMADR BAR | Directly supported by KGDB arithmetic and PCI BAR data |
| PHYS_TO_VM_PAGE returned a dense-array slot that was not a valid initialized page | Strongly supported by vm_page_array index and descriptor contents |
| Invalid page->object/busy state caused the panic | Strongly supported by both panic stacks and descriptor fields |
| A special-PTE path with fictitious-page bookkeeping is the correct architectural direction | Supported by Linux semantics, FreeBSD VM APIs, and D59883 prototype; still requires review and validation |
| The proposed patch is safe for all DRM users or all architectures | Not established |
| The proposed patch eliminates RCS0 hangs | Not established |

## 7. Immediate safety controls

- Keep both vmcore files and both core.txt files unchanged.
- Do not repeat the FFmpeg hwupload test on the active installation.
- Do not unload i915kms or change the active display GPU as part of this source investigation.
- Do not install or load a new kernel/module until patch review, build validation, and an explicit deployment decision.
- Keep the pre-test ZFS snapshot as the user's recovery point. Its exact snapshot name and creation metadata still need independent capture before any future deployment.
- Keep the GEM backing-page patch series separate from this GTT special-PTE series.
