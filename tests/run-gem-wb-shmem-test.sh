#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
TEST_BIN="${TEST_BIN:-$(dirname "$0")/gem_wb_shmem_roundtrip}"
MOUNTPOINT="${MOUNTPOINT:-/mnt/lindebugfs}"
MODULE="${MODULE:-/boot/modules/i915kms.ko}"
EXPECTED_SHA256="${EXPECTED_SHA256:-}"
ITERATIONS="${ITERATIONS:-1}"
DROP_CACHES_NODE="${DROP_CACHES_NODE:-$MOUNTPOINT/dri/drmn0/i915_gem_drop_caches}"

[ "$(id -u)" -eq 0 ] || { echo 'Run as root (su -)' >&2; exit 2; }
[ -x "$TEST_BIN" ] || { echo "Missing test binary: $TEST_BIN (run make -C tests)" >&2; exit 2; }
case "$ITERATIONS" in ''|*[!0-9]*) echo 'ITERATIONS must be a positive integer' >&2; exit 2;; esac
[ "$ITERATIONS" -gt 0 ] || { echo 'ITERATIONS must be greater than zero' >&2; exit 2; }
[ -r "$MODULE" ] || { echo "Module file not readable: $MODULE" >&2; exit 2; }
if [ -n "$EXPECTED_SHA256" ]; then
  actual=$(sha256 -q "$MODULE")
  [ "$actual" = "$EXPECTED_SHA256" ] || { echo "Module file hash mismatch: $actual" >&2; exit 2; }
fi
kldstat | grep -q 'i915kms.ko' || { echo 'i915kms.ko is not loaded' >&2; exit 2; }

kldload lindebugfs 2>/dev/null || true
mounted_here=0
if ! mount | grep -Fq "lindebugfs on $MOUNTPOINT "; then
  mkdir -p "$MOUNTPOINT"
  mount -t lindebugfs lindebugfs "$MOUNTPOINT"
  mounted_here=1
fi
cleanup() {
  if [ "$mounted_here" -eq 1 ]; then
    umount "$MOUNTPOINT"
  fi
}
trap cleanup EXIT HUP INT TERM
[ -e "$DROP_CACHES_NODE" ] || { echo "Missing i915 debugfs node: $DROP_CACHES_NODE" >&2; exit 2; }

printf '%s\n' '=== test metadata ==='
date -u
freebsd-version
printf 'module_file=%s\n' "$MODULE"
printf 'module_file_sha256=%s\n' "$(sha256 -q "$MODULE")"
printf 'loaded_modules:\n'
kldstat | grep -E 'i915kms|drm\.ko'
printf 'iterations=%s\n' "$ITERATIONS"
printf '%s\n' 'NOTE: module-file hash identifies the on-disk file, not necessarily the already-loaded kernel image.'
printf '%s\n' '=== GEM objects before test ==='
cat "$MOUNTPOINT/dri/drmn0/i915_gem_objects"

n=1
while [ "$n" -le "$ITERATIONS" ]; do
  printf '=== WB mmap / WILLNEED / DROP_UNBOUND test %s/%s ===\n' "$n" "$ITERATIONS"
  DROP_CACHES_NODE="$DROP_CACHES_NODE" "$TEST_BIN"
  n=$((n + 1))
done

printf '%s\n' '=== GPU hang messages (recent ring buffer) ==='
dmesg | grep -Ei 'GPU HANG|Resetting rcs0' | tail -20 || true
