# BusyBox — Alpine edge patch set adoption

Port report for the migration from a hand-maintained patch set (4 inline
seds + 5 Debian CVE backports) to the Alpine edge patch set, while
preserving the gateway's curated applet selection.

**Date:** 2026-04-17
**BusyBox version:** 1.37.0 (no bump — Alpine edge ships the same version)
**Patch count:** 24 (17 Alpine + 3 CVE supplements + 4 Lexra)

---

## Rationale

Same philosophy as the toolchain migration (GCC / binutils / musl moved
to Alpine edge in v3.0.0): inherit Alpine's curated patch stream — CVE
backports, musl-first fixes, hardening defaults — without depending on
Alpine infrastructure (`utmps`, external `ssl_client`, multi-variant
builds). Drop the locally maintained patches whose fixes are now carried
upstream (or by Alpine), keep only what remains truly gateway-specific.

---

## Patches imported from Alpine (17)

Alpine ships **44 patches** for BusyBox 1.37.0. We kept the subset that
is portable to our target (MIPS big-endian, musl, crosstool-NG build)
and does not require Alpine-specific infrastructure.

| # | File (renumbered) | Upstream origin | Why we took it |
|---|-------------------|-----------------|----------------|
| 001 | alpine-awk-fix-handling-of-literal-backslashes-in-replaceme | upstream bug fix | awk replacement string regression (post-1.37.0) |
| 002 | alpine-hexdump-fix-regression-with-n4-e-u | upstream bug fix | hexdump `-n4 -e '"%u"'` behavior regression |
| 003 | alpine-mount-fix-parsing-proc-mounts-with-long-lines | upstream bug fix | `mount` crashed on `/proc/mounts` lines > 256 chars |
| 004 | alpine-netstat-sanitize-process-names | CVE-2024-58251 | netstat printed non-printable chars from `/proc/PID/comm` |
| 005 | alpine-tar-fix-16018-masking-of-potentially-malicious-tar-cpio-content | CVE-2025-46394 | terminal-escape injection via crafted tar/cpio filenames |
| 006 | alpine-tar-fix-TOCTOU-symlink-race-condition | security | `O_NOFOLLOW` for regular file writes during tar extraction |
| 007 | alpine-tunctl-fix-segfault-on-ioctl-failure | upstream bug fix | NULL fmt-string deref on ioctl() error path |
| 008 | alpine-wget-add-header-Accept | hardening/compat | some CDNs reject requests without an `Accept:` header |
| 009 | alpine-libbb-sockaddr2str-ensure-only-printable-characters | hardening | defense in depth against terminal-escape injection |
| 010 | alpine-nslookup-sanitize-all-printed-strings-with-printable | hardening | same as 009, applied in nslookup |
| 011 | alpine-ping-make-ping-work-without-root-privileges | hardening | `IPPROTO_ICMP DGRAM` socket → no SUID root required |
| 012 | alpine-find-fix-xdev-depth-and-delete | upstream bug fix | `find -xdev -depth -delete` crossed filesystem boundaries |
| 013 | alpine-awk.c-fix-CVE-2023-42366-bug-15874 | CVE-2023-42366 | awk OOB write on crafted input (heap corruption) |
| 014 | alpine-ash-reject-unknown-long-options | hardening | ash silently ignored unknown `--options`, masking bugs |
| 015 | alpine-syslogd-fix-wrong-OPT_locallog-flag-detection | upstream bug fix | `-l` flag (local log) was never detected |
| 016 | alpine-lineedit-fix-some-tab-completions-written-to-stdout | upstream bug fix | tab completion noise appeared in stdout redirections |
| 017 | alpine-lineedit-use-stdout-for-shell-history-builtin | upstream bug fix | `history` builtin wrote to stderr instead of stdout |

Naming: Alpine filenames are kept as-is (minus the `0001-…0035-`
prefix, which is Alpine-internal ordering). Our `001-017` prefix
reflects the application order.

---

## Alpine patches NOT imported (27 of 44)

These were evaluated and dropped — they either target infrastructure we
don't ship or modify behavior in ways that would surprise users of the
curated gateway rootfs.

### Architecture-specific (1)

| Alpine file | Why dropped |
|-------------|-------------|
| 0023-Hackfix-to-disable-HW-acceleration-for-MD5-SHA1-on-x86 | x86-only; our target is big-endian MIPS |

### Alpine-infra dependencies (3)

| Alpine file | Why dropped |
|-------------|-------------|
| 0004-Avoid-redefined-warnings-when-buiding-with-utmps | requires `libutmps`/`utmps-dev`, not shipped on the gateway |
| 0009-properly-fix-wget-https-support | relies on external `ssl_client` binary (subpackage on Alpine); we don't ship it |
| 0012-ash-exec-busybox.static | requires the separate `busybox.static` subpackage |

### Testsuite-only patches (4)

The gateway build doesn't run `make test`; these only fix test-runner
quirks on Alpine's CI.

| Alpine file | Why dropped |
|-------------|-------------|
| 0021-tests-fix-tarball-creation | testsuite only |
| 0022-tests-musl-doesn-t-seem-to-recognize-UTC0-as-a-timezone | testsuite only |
| 0027-awk-Mark-test-for-handling-of-start-of-word-pattern | testsuite only |
| 0028-od-Skip-od-B-on-big-endian | testsuite only (even though we ARE big-endian, we don't run tests) |
| 0030-hexdump-Skip-a-single-test-on-big-endian-systems | testsuite only |

### Applet install-path changes (4)

Alpine ships a different filesystem layout than our rootfs; relocating
applets would confuse scripts and muscle-memory.

| Alpine file | Why dropped |
|-------------|-------------|
| 0007-nologin-Install-applet-to-sbin-instead-of-usr-sbin | our layout uses `/sbin` already via busybox.config |
| 0013-app-location-for-cpio-vi-and-lspci | installs to `/sbin` on Alpine; we don't ship lspci at all |
| 0001-blkdiscard-ship-link-to-sbin-instead-of-usr-bin | we don't ship blkdiscard |
| 0035-cpio-map-F-to-file-long-option | cosmetic longopt alias, not worth the diff |

### Applet behavior / policy changes (15)

Alpine's defaults diverge from the gateway's curated UX. These patches
either change user-visible behavior, or require adduser/passwd setups
that don't match our `/etc/passwd` and init scripts.

| Alpine file | Why dropped |
|-------------|-------------|
| 0002-adduser-default-to-sbin-nologin | our /etc/passwd uses `/bin/false` for system users |
| 0003-ash-add-built-in-BB_ASH_VERSION | no consumer of that variable in our scripts |
| 0006-modinfo-add-k-option-for-kernel-version | we don't ship modinfo on the gateway |
| 0008-pgrep-add-support-for-matching-against-UID-and-RUID | minor feature; our scripts don't need it |
| 0010-fsck-resolve-LABEL-.-UUID-.-spec-to-device | fsck not used on JFFS2/SquashFS |
| 0014-udhcpc-set-default-discover-retries-to-5 | our udhcpc config already sets retries explicitly |
| 0016-fbsplash-support-console-switching | no framebuffer on the gateway |
| 0017-fbsplash-support-image-and-bar-alignment-and-positio | no framebuffer |
| 0018-depmod-support-generating-kmod-binary-index-files | we use in-tree modules only |
| 0019-Add-flag-for-not-following-symlinks-when-recursing | diff-only feature, not needed |
| 0020-udhcpc-Don-t-background-if-n-is-given | our scripts don't use `udhcpc -n` |
| 0024-umount-Implement-O-option-to-unmount-by-mount-option | niche feature, not used |
| 0034-adduser-remove-preconfigured-GECOS-full-name-field | cosmetic |
| 0001-init-add-support-for-separate-reboot-action | our init layout is different (userdata S9x scripts) |
| 0001-tests-chmod-fix | testsuite only |

---

## CVE supplements we kept (3)

Three of the five original Debian-style CVE patches are **not covered**
by the Alpine set and have been retained with an `800-` prefix. Verified
by reading each Alpine patch and comparing affected files.

| # | File | CVE | Why kept |
|---|------|-----|----------|
| 800 | CVE-2023-39810-path-traversal-protection | CVE-2023-39810 | introduces the `FEATURE_PATH_TRAVERSAL_PROTECTION` Kconfig framework. Our `busybox.config` enables it (`CONFIG_FEATURE_PATH_TRAVERSAL_PROTECTION=y`), so 801/802 depend on it. Alpine does not ship this framework. |
| 801 | CVE-2026-26157-tar-hardlink-path-traversal | CVE-2026-26157 + CVE-2026-26158 | strips unsafe hardlink components (`../etc/hosts` → fail) like GNU tar does. Alpine's `006-tar-fix-TOCTOU-symlink-race-condition` addresses a different vector (`O_NOFOLLOW` on regular files) and does not protect hardlinks. |
| 802 | CVE-2026-26158-fix-symlink-target-stripping | CVE-2026-26158 | follow-up to 801 — only strip unsafe components from hardlinks, not symlinks (fixes the "Symlinks and hardlinks coexist" tar regression introduced by 801). |

Application order matters: 800 creates the symbol 801 consumes;
801's strip logic is then corrected by 802. They are applied **after**
all Alpine patches because one of Alpine's patches (`006-tar-fix-TOCTOU`)
also touches `archival/libarchive/data_extract_all.c` in a different
region, and the `a/b/` hunk context remains valid in both orders.

### Debian CVE patches dropped (2)

| Original file | Why dropped |
|---------------|-------------|
| 02-CVE-2025-46394-tar-escape-sequence-sanitize | covered by Alpine 005-tar-fix-16018 (same bug, same fix) |
| 03-CVE-2025-46394-tar-escape-sequence-sanitize-part2 | covered by Alpine 005 (both files patched in one go) |

---

## Lexra platform patches (4)

The 4 previously inline `sed` edits in `build_busybox.sh` have been
converted to proper `.patch` files (numbered 900-903), generated via
`diff -u` against a clean upstream copy. Converting them yields better
debuggability (we know exactly when a patch fails to apply after a
version bump) and symmetry with our toolchain patch stack.

| # | File | Target | Purpose |
|---|------|--------|---------|
| 900 | Lexra-off_t-size-check | `include/libbb.h` | comments out `BUG_off_t_size_is_misdetected` — musl MIPS mis-detects `sizeof(off_t)` vs `sizeof(uoff_t)` at compile time; the struct is a compile-time assertion, not a real bug. |
| 901 | Lexra-PAGE_SIZE-fallback | `scripts/generate_BUFSIZ.sh` | forces `PAGE_SIZE=1000` then `=4096` fallbacks. During cross-compilation `getconf PAGESIZE` queries the *host* machine, not the target — we need to force a sensible value. |
| 902 | Lexra-jffs2-fcntl-lock | `libbb/update_passwd.c` | silently ignore `fcntl(F_SETLK)` failures. JFFS2 does not implement file locking; the default warning spams once per `passwd`/`adduser`/`addgroup` invocation. |
| 903 | Lexra-usage-write-fortify | `applets/usage.c` | check the return value of `write()`. GCC 15 + `-D_FORTIFY_SOURCE=2` (Alpine's default, enabled by our toolchain patch 004-alpine) rejects unchecked `write()` with `warning: ignoring return value`. |

900-903 are applied **last**: they are platform adaptations, not bug
fixes, and should sit on top of the upstream + Alpine + CVE stack.

---

## `build_busybox.sh` refactor

- 285 → 226 lines.
- Removed: 4 inline `sed` blocks (lines 117-164 of the previous version).
  Each one now lives as a standalone, reviewable `.patch` file.
- Replaced: two-phase "inline then security" patch logic, with one
  alphabetical loop over `patches/*.patch`.
- Added: hard-fail on patch application error (the previous version
  only printed a warning on failure, so a silently-broken patch could
  ship a half-patched source tree). We now `exit 1` with the last 10
  lines of patch output on any failure.
- Unchanged: tarball download, toolchain auto-detection, menuconfig
  flow, double-make (for `COMMON_BUFSIZE` optimization), install to
  `${ROOTFS_DIR}`, applet count verification.

---

## Verification

End-to-end build on 2026-04-17:

- All 24 patches apply sequentially with zero conflicts.
- `busybox` binary: **ELF 32-bit MSB, MIPS-I, statically linked, 770 KB.**
- **100 applets** installed (unchanged vs. previous build — confirms the
  curated `busybox.config` selection was preserved end-to-end).
- Three new symlinks appeared in `skeleton/sbin/`: `ipaddr`, `iplink`,
  `ipneigh`. These are already-selected `ip` subcommand aliases that
  upstream BusyBox now exposes as separate install paths; no config
  change drove this.

---

## Out of scope

- Version bump to 1.38+ — not yet in Alpine edge.
- Static-build variant — Alpine ships three BusyBox binaries (`/bin/busybox`,
  `busybox-extras`, `busybox.static`). We keep a single dynamic build
  to minimise the rootfs footprint (musl is already shipped for other
  userspace apps).
- HTTPS in wget via external `ssl_client` — requires a separate binary
  and Alpine-specific split.
- `utmps` / `wtmp` — not used on the gateway.
