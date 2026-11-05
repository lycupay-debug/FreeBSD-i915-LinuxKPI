#!/bin/sh
set -eu
TEST_BIN="${TEST_BIN:-$(dirname "$0")/gem_wb_shmem_roundtrip}"
MOUNTPOINT="${MOUNTPOINT:-/mnt/lindebugfs}"
MODULE="${MODULE:-/boot/modules/i915kms.ko}"
EXPECTED_SHA256="${EXPECTED_SHA256:-}"
[ "$(id -u)" -eq 0 ] || { echo 'Run as root (su -)' >&2; exit 2; }
[ -x "$TEST_BIN" ] || { echo "Missing test binary: $TEST_BIN" >&2; exit 2; }
if [ -n "$EXPECTED_SHA256" ]; then
  actual=$(sha256 -q "$MODULE")
  [ "$actual" = "$EXPECTED_SHA256" ] || { echo "Module hash mismatch: $actual" >&2; exit 2; }
fi
kldstat | grep -q 'i915kms.ko' || { echo 'i915kms.ko is not loaded' >&2; exit 2; }
kldload lindebugfs 2>/dev/null || true
mounted_here=0
if ! mount | grep -q "lindebugfs on $MOUNTPOINT"; then
  mkdir -p "$MOUNTPOINT"
  mount -t lindebugfs lindebugfs "$MOUNTPOINT"
  mounted_here=1
fi
cleanup() { if [ "$mounted_here" -eq 1 ]; then umount "$MOUNTPOINT"; fi; }
trap cleanup EXIT HUP INT TERM
NODE="$MOUNTPOINT/dri/drmn0/i915_gem_drop_caches"
[ -e "$NODE" ] || { echo "Missing i915 debugfs node: $NODE" >&2; exit 2; }
echo '=== loaded modules ==='
kldstat | grep -E 'i915kms|drm\.ko'
echo '=== GEM objects ==='
cat "$MOUNTPOINT/dri/drmn0/i915_gem_objects"
echo '=== WB mmap / WILLNEED / DROP_UNBOUND test ==='
"$TEST_BIN"
echo '=== GPU hang messages ==='
dmesg | grep -Ei 'GPU HANG|Resetting rcs0' | tail -20 || true
