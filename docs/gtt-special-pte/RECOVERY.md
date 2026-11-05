# Source recovery and repository workflow

This document describes recovery of the research/patch work after a system snapshot rollback. It does not install or roll back kernel modules.

## 1. Repository and branch

Repository:

    https://github.com/lycupay-debug/FreeBSD-i915-LinuxKPI

Working branch:

    gtt-special-pte-20261105

The GTT special-PTE work is separate from the existing main-branch GEM shmem-page patch.

The branch is now published on GitHub. Use git ls-remote origin refs/heads/gtt-special-pte-20261105 to inspect its current head. The source can be restored after a system snapshot rollback by cloning or fetching the repository again.

## 2. Fresh clone

Run as the normal user:

    git clone https://github.com/lycupay-debug/FreeBSD-i915-LinuxKPI.git
    cd FreeBSD-i915-LinuxKPI
    git switch gtt-special-pte-20261105

## 3. Existing clone after snapshot recovery

If the repository directory survived the rollback:

    cd ~/Projects/FreeBSD-i915-LinuxKPI
    git fetch origin
    git switch gtt-special-pte-20261105
    git pull --ff-only origin gtt-special-pte-20261105

If the directory is missing or damaged, use the fresh-clone procedure instead.

## 4. Patch order

Patch 1 applies to the FreeBSD base source tree:

    patches/gtt-special-pte/0001-linuxkpi-special-pte-api.patch

Patch 2 applies to the matching drm-kmod source tree:

    patches/gtt-special-pte/0002-i915-use-special-pte.patch

The complete series must be reviewed and applied in order. Do not apply only the i915 caller patch: the new LinuxKPI API does not exist without patch 1.

## 5. Dry-run commands

FreeBSD base source tree:

    cd /usr/src
    patch -p1 --dry-run < ~/Projects/FreeBSD-i915-LinuxKPI/patches/gtt-special-pte/0001-linuxkpi-special-pte-api.patch

Matching drm-kmod source tree:

    cd /path/to/matching/drm-kmod-source
    patch -p1 --dry-run < ~/Projects/FreeBSD-i915-LinuxKPI/patches/gtt-special-pte/0002-i915-use-special-pte.patch

The path above is a placeholder. Confirm the exact source tree and revision before running.

## 6. Reversing source patches

If the patches were applied to source trees and a source-only rollback is required, reverse them in reverse order:

    cd /path/to/matching/drm-kmod-source
    patch -R -p1 < ~/Projects/FreeBSD-i915-LinuxKPI/patches/gtt-special-pte/0002-i915-use-special-pte.patch

    cd /usr/src
    patch -R -p1 < ~/Projects/FreeBSD-i915-LinuxKPI/patches/gtt-special-pte/0001-linuxkpi-special-pte-api.patch

This reverses source edits only. It does not restore installed kernel files, modules, loader settings, or the running kernel.

## 7. Snapshot recovery versus Git recovery

A ZFS snapshot restores system state. Git preserves the source and documentation state that has been committed and pushed.

The two mechanisms are complementary:

- Snapshot: recover the machine to a known system state.
- GitHub: recover committed patch source, investigation notes, compatibility records, and validation history.

Uncommitted files are not guaranteed to survive a snapshot rollback. Push important work before performing any rollback.

The user reported that a snapshot was created before the FFmpeg test. Its exact snapshot name, creation time, and contents have not yet been independently captured in this repository. Record those details before a future deployment.

## 8. Safety boundary

These instructions do not authorize patch application, kernel build, module installation, module loading, service restart, or reboot. Those are separate operations requiring review and an explicit deployment decision.

## 9. Published branch state — 2026-11-05

The GitHub branch is published:

    gtt-special-pte-20261105

Published history includes the initial source and documentation commit (94a42eda50c08d3d4e5d13aea2e37e79cbc94dac), the executable-mode correction (d58e8098a317fe800da8a26b90fc689e34c70059), and the recovery-document update (40a102c9bf691d539275b1cfc2541f474c837c6e).

The laptop's local working branch contains the same source tree under local commit 9865db7 (tree e31ed165262564a9d94e822dbb134ff1878a8676). The GitHub connector published an equivalent tree with a different commit history. Direct git push/fetch from the laptop encountered an HTTPS TLS unexpected-EOF error; remote publication was verified through the connected GitHub integration.

For a fresh clone, use the normal clone procedure above. If an existing clone already has a local branch named gtt-special-pte-20261105 and that branch differs from origin after fetch, do not run pull blindly. First preserve the local branch:

    git status --short
    git branch -m gtt-special-pte-20261105 gtt-special-pte-20261105-local-backup
    git switch --track -c gtt-special-pte-20261105 origin/gtt-special-pte-20261105

Only rename the local branch after confirming there are no uncommitted changes. This preserves the local history while creating a clean tracking branch from GitHub.
