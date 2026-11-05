# GTT special-PTE patch checks

This directory contains source-only checks for the GTT special-PTE patch series.

## Dry-run check

Set DRM_SRC to the root of the matching drm-kmod source tree. FREEBSD_SRC defaults to /usr/src.

    DRM_SRC=/path/to/drm-kmod-source sh tests/gtt-special-pte/check-patches.sh

For the source tree used during the 2026-11-05 investigation:

    DRM_SRC=/usr/local/src/i915-gem-patch/exact-6.12.85_3/drm-kmod-drm_v6.12.85_3 sh tests/gtt-special-pte/check-patches.sh

The script only runs patch --dry-run. It does not modify either source tree, build a kernel or module, install/load a module, reboot, or exercise the GPU.

A successful dry run verifies patch context only. It does not prove the code compiles or behaves correctly.
