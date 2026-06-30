#!/usr/bin/env bash
# scripts/smoke_p1.sh -- Cinux-GUI P1 Probe-1 fbdev-host QEMU smoke harness.
#
# Boots Alpine "virt" under QEMU with a vesafb framebuffer (-vga std -> /dev/fb0)
# + an absolute-pointer usb-tablet (-> /dev/input/eventN, EV_ABS) + VNC, shares
# the repo via a read-only 9p mount, and asks you (over VNC) to run the guest
# script that builds + runs fbdev-host. This harness captures the serial console
# (gate signals), grabs a PPM screenshot, and prints a pass/fail verdict.
#
# Semi-automated BY DESIGN (P1 is a manual smoke): the host prepares + gates,
# you drive the guest via VNC. Full apkovl auto-exec is a recorded TODO.
#
# Usage:  ./scripts/smoke_p1.sh
# Exit:   0 PASS (boot + devices + fbdev-host loop + no-crash), 1 FAIL/error.
# Cache:  build/smoke/ (ISO cached + sha256-verified; per-run scratch in run/).

set -euo pipefail

# ── 0. config (pin these; a re-pin is a one-line change) ────────────────────
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly SMOKE_DIR="$ROOT/build/smoke"
readonly RUN_DIR="$SMOKE_DIR/run"
readonly SERIAL_SOCK="$RUN_DIR/serial.sock"
readonly SERIAL_LOG="$RUN_DIR/serial.log"
readonly MONITOR_SOCK="$RUN_DIR/monitor.sock"
readonly QEMU_PIDFILE="$RUN_DIR/qemu.pid"
readonly SHOT_PPM="$RUN_DIR/shot.ppm"

readonly ALPINE_VER="3.21.3"
readonly ALPINE_ISO="alpine-virt-${ALPINE_VER}-x86_64.iso"
readonly ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.21/releases/x86_64/${ALPINE_ISO}"
# Pin the sha256 after the first download (the script prints it). The placeholder
# means "trust + record on first run"; replace the literal to enforce on re-runs.
readonly ALPINE_SHA256="<FILL>"

readonly QEMU_BIN="qemu-system-x86_64"
readonly VNC_DISP="127.0.0.1:99"  # :99 => TCP 5999; far from CinuxOS's default :0 (5900)
readonly SRC_TAG="host0"          # 9p mount tag for $ROOT inside the guest

log() { printf '[smoke] %s\n' "$*" >&2; }
die() { log "FAIL: $*"; cleanup; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing tool: $1 (install $2)"; }

cleanup() {
  if [[ -S "$MONITOR_SOCK" ]]; then
    printf 'quit\n' | socat - UNIX-CONNECT:"$MONITOR_SOCK" >/dev/null 2>&1 || true
  fi
  if [[ -f "$QEMU_PIDFILE" ]]; then
    local pid; pid=$(cat "$QEMU_PIDFILE" 2>/dev/null || true)
    [[ -n "${pid:-}" ]] && kill "$pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# ── 1. preflight: host tools + a host-side fbdev-host build (sanity) ─────────
log "preflight: host tools"
need "$QEMU_BIN" qemu-system-x86
need wget wget
need sha256sum coreutils
need socat socat
need bsdtar libarchive-tools
need python3 python3

log "preflight: host-side fbdev-host build (smoke sanity)"
(cd "$ROOT" && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
   && cmake --build build -j"$(nproc)" --target fbdev-host >/dev/null 2>&1) \
   || die "host-side fbdev-host build failed"

# ── 2. acquire + cache the Alpine ISO (idempotent, sha256-verified) ──────────
mkdir -p "$SMOKE_DIR" "$RUN_DIR"
rm -f "$RUN_DIR"/*

if [[ ! -f "$SMOKE_DIR/$ALPINE_ISO" ]]; then
  log "acquire: downloading $ALPINE_ISO (cached for re-runs)"
  wget -q -O "$SMOKE_DIR/$ALPINE_ISO" "$ALPINE_URL" || die "download failed: $ALPINE_URL"
else
  log "acquire: cached ISO present"
fi

actual_sha=$(sha256sum "$SMOKE_DIR/$ALPINE_ISO" | awk '{print $1}')
if [[ "$ALPINE_SHA256" == "<FILL>"* ]]; then
  log "verify: ALPINE_SHA256 placeholder -- trusting first download ($actual_sha)"
  log "  pin it: edit scripts/smoke_p1.sh -> ALPINE_SHA256=\"$actual_sha\""
else
  [[ "$actual_sha" == "$ALPINE_SHA256" ]] \
    || die "sha256 mismatch: got $actual_sha, want $ALPINE_SHA256 (delete ISO to re-download)"
  log "verify: sha256 OK"
fi

# ── 3. (optional) extract direct-boot kernel/initramfs for fast boot ─────────
VMLINUZ="$SMOKE_DIR/boot/vmlinuz-virt"
INITRAMFS="$SMOKE_DIR/boot/initramfs-virt"
if [[ ! -f "$VMLINUZ" || ! -f "$INITRAMFS" ]]; then
  bsdtar -C "$SMOKE_DIR" -xf "$SMOKE_DIR/$ALPINE_ISO" boot/vmlinuz-virt boot/initramfs-virt \
    2>/dev/null && log "extract: vmlinuz-virt + initramfs-virt (direct-boot path)" || true
fi

# ── 4. launch QEMU (daemonized; host keeps driving it via monitor) ───────────
QEMU_ARGS=(
  -m 512
  -vga std
  -device qemu-xhci,id=xhci
  -device usb-tablet
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0
  -fsdev "local,id=srcdev,path=$ROOT,security_model=none"
  -device "virtio-9p-pci,fsdev=srcdev,mount_tag=$SRC_TAG"
  -display "vnc=$VNC_DISP"
  -serial "unix:$SERIAL_SOCK,server,nowait"
  -monitor "unix:$MONITOR_SOCK,server,nowait"
  -pidfile "$QEMU_PIDFILE"
  -daemonize
)
if [[ -f "$VMLINUZ" && -f "$INITRAMFS" ]]; then
  QEMU_ARGS+=(
    -kernel "$VMLINUZ"
    -initrd "$INITRAMFS"
    -append "console=ttyS0 quiet alpine_dev=cdrom:iso9660 modloop=/boot/modloop-virt modules=loop,squashfs,sd-mod,usb-storage,virtio-pci,virtio_blk,9p,9pnet_virtio"
    -drive "file=$SMOKE_DIR/$ALPINE_ISO,format=raw,if=virtio,media=cdrom,readonly=on"
  )
else
  QEMU_ARGS+=( -drive "file=$SMOKE_DIR/$ALPINE_ISO,format=raw,if=virtio,media=cdrom,readonly=on" )
fi

"$QEMU_BIN" "${QEMU_ARGS[@]}" || die "qemu launch failed"
log "qemu: pid=$(cat "$QEMU_PIDFILE") vnc=$VNC_DISP (TCP 5900) serial=$SERIAL_LOG"

# ── 5. drive the guest FROM THIS CONSOLE (serial socket + drive.py) ──────────
# No VNC clicking needed: drive.py logs in over serial, mounts the 9p source
# share, runs the guest smoke script, and streams everything (boot + apk + build
# + fbdev-host) back to this console. VNC is OPTIONAL -- only for eyeballing the
# framebuffer + wiggling the mouse; pass/fail does not depend on it.
cat >&2 <<EOF

[smoke] Optional: a VNC viewer to $VNC_DISP (localhost:5999) shows the live
[smoke] framebuffer + takes mouse input while fbdev-host runs. Not required.
[smoke] Streaming the guest console to this host console now...

EOF

log "drive: boot + login + mount 9p + run guest script (serial -> this console)"
python3 "$ROOT/scripts/smoke_p1_drive.py" "$SERIAL_SOCK" "$SERIAL_LOG" \
  || log "drive.py returned non-zero (timeout / guest error) -- evaluating gates anyway"

# ── 7. screenshot mid-run (diagnostic only -- NOT gating) ────────────────────
if grep -q 'GUEST_RUN_START' "$SERIAL_LOG" 2>/dev/null && [[ -S "$MONITOR_SOCK" ]]; then
  log "shot: screendump -> $SHOT_PPM"
  printf 'screendump %s\n' "$SHOT_PPM" | socat - UNIX-CONNECT:"$MONITOR_SOCK" >/dev/null 2>&1 || true
fi

# ── 8. gates (deterministic pass/fail from serial markers) ───────────────────
log "gate: evaluating serial log"
ok=1
grep -q 'GUEST_BUILD_OK' "$SERIAL_LOG" 2>/dev/null \
  || { log "FAIL gate1: guest did not build fbdev-host"; ok=0; }
grep -q 'GUEST_FB0_OK' "$SERIAL_LOG" 2>/dev/null \
  || { log "FAIL gate2a: no /dev/fb0 (vesafb?)"; ok=0; }
grep -q 'GUEST_TABLET_OK' "$SERIAL_LOG" 2>/dev/null \
  || { log "FAIL gate2b: no tablet event node"; ok=0; }
grep -Eq 'fbdev-host: fb [0-9]+x[0-9]+ stride' "$SERIAL_LOG" 2>/dev/null \
  || { log "FAIL gate3a: fbdev-host did not open /dev/fb0"; ok=0; }
grep -q 'cannot open' "$SERIAL_LOG" 2>/dev/null \
  && { log "FAIL gate3b: fbdev-host reported 'cannot open'"; ok=0; }
if grep -Eq 'Segmentation fault|Bus error|kernel BUG|Oops:' "$SERIAL_LOG" 2>/dev/null; then
  log "FAIL gate4: crash signature in serial log"; ok=0;
fi
echo "[smoke]   fb0:    $(grep 'GUEST_FB0_OK' "$SERIAL_LOG" 2>/dev/null | tail -1)" >&2
echo "[smoke]   tablet: $(grep 'GUEST_TABLET_EVENT=' "$SERIAL_LOG" 2>/dev/null | tail -1)" >&2
echo "[smoke]   rc:     $(grep 'GUEST_RUN_RC=' "$SERIAL_LOG" 2>/dev/null | tail -1)  (124 = timeout, expected)" >&2

# ── 9. verdict ───────────────────────────────────────────────────────────────
if [[ "$ok" -eq 1 ]]; then
  log "PASS -- boot + devices + fbdev-host loop, no crash."
  [[ -f "$SHOT_PPM" ]] && log "  screenshot: $SHOT_PPM (view / convert to png to eyeball)"
  log "  serial log: $SERIAL_LOG"
  exit 0
fi
log "FAIL -- see $SERIAL_LOG; $SHOT_PPM (if present) for the last frame."
exit 1
