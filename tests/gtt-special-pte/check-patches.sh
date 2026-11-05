#!/bin/sh
set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
FREEBSD_SRC=$(env | sed -n 's/^FREEBSD_SRC=//p')
DRM_SRC=$(env | sed -n 's/^DRM_SRC=//p')
[ -n "$FREEBSD_SRC" ] || FREEBSD_SRC=/usr/src

if [ -z "$DRM_SRC" ]; then
    echo "ERROR: set DRM_SRC to the matching drm-kmod source root." >&2
    exit 2
fi

if [ ! -f "$FREEBSD_SRC/sys/compat/linuxkpi/common/src/linux_page.c" ]; then
    echo "ERROR: FREEBSD_SRC does not look like a FreeBSD source tree: $FREEBSD_SRC" >&2
    exit 2
fi

if [ ! -f "$DRM_SRC/drivers/gpu/drm/i915/i915_mm.c" ]; then
    echo "ERROR: DRM_SRC does not look like a drm-kmod source tree: $DRM_SRC" >&2
    exit 2
fi

echo "[1/2] FreeBSD LinuxKPI/VM patch dry run"
(
    cd "$FREEBSD_SRC"
    patch -p1 --dry-run < "$REPO_ROOT/patches/gtt-special-pte/0001-linuxkpi-special-pte-api.patch"
)

echo "[2/2] drm-kmod i915 caller patch dry run"
(
    cd "$DRM_SRC"
    patch -p1 --dry-run < "$REPO_ROOT/patches/gtt-special-pte/0002-i915-use-special-pte.patch"
)

echo "PASS: both patch contexts apply cleanly in dry-run mode."
echo "This script does not apply patches, build code, install modules, or run GPU tests."
