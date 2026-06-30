#!/usr/bin/env bash
# scripts/smoke_p1.sh -- Cinux-GUI P1 Probe-1 fbdev-host QEMU smoke harness.
#
# Boots Alpine "virt" under QEMU (vesafb /dev/fb0 + usb-tablet EV_ABS + VNC),
# starts a host HTTP server, and the guest (driven over serial by drive.py)
# fetches the source over HTTP (QEMU user-net 10.0.2.2), builds fbdev-host
# natively, and runs it. 9p was unreliable (driver bound but tag never matched);
# HTTP is the source transport. Pass/fail is gated on serial markers; a PPM
# screenshot is grabbed for optional eyeballing.
#
# Usage:  ./scripts/smoke_p1.sh
# Exit:   0 PASS (boot + devices + fbdev-host loop + no-crash), 1 FAIL/error.
# Cache:  build/smoke/ (ISO + src.tar.gz cached; per-run scratch in run/).

set -euo pipefail

# ── 0. config ───────────────────────────────────────────────────────────────
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
readonly ALPINE_SHA256="<FILL>"   # pin after first download (script prints it)

readonly QEMU_BIN="qemu-system-x86_64"
readonly VNC_DISP="127.0.0.1:99"  # :99 => TCP 5999; far from CinuxOS default :0 (5900)
readonly HTTP_PORT="8765"         # host http.server; guest fetches via 10.0.2.2

HTTP_PID=""

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
  [[ -n "${HTTP_PID:-}" ]] && kill "$HTTP_PID" 2>/dev/null || true
}
trap cleanup EXIT

# ── 1. preflight ────────────────────────────────────────────────────────────
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

# ── 2. acquire + cache ISO ──────────────────────────────────────────────────
mkdir -p "$SMOKE_DIR" "$RUN_DIR"
rm -f "$RUN_DIR"/*

if [[ ! -f "$SMOKE_DIR/$ALPINE_ISO" ]]; then
  log "acquire: downloading $ALPINE_ISO (cached for re-runs)"
  wget -q -O "$SMOKE_DIR/$ALPINE_ISO" "$ALPINE_URL" || die "download failed"
else
  log "acquire: cached ISO"
fi
actual_sha=$(sha256sum "$SMOKE_DIR/$ALPINE_ISO" | awk '{print $1}')
if [[ "$ALPINE_SHA256" == "<FILL>"* ]]; then
  log "verify: placeholder -- trusting first download ($actual_sha); pin it in the script"
else
  [[ "$actual_sha" == "$ALPINE_SHA256" ]] \
    || die "sha256 mismatch: got $actual_sha, want $ALPINE_SHA256"
  log "verify: sha256 OK"
fi

# ── 3. extract direct-boot kernel/initramfs (optional fast path) ────────────
VMLINUZ="$SMOKE_DIR/boot/vmlinuz-virt"; INITRAMFS="$SMOKE_DIR/boot/initramfs-virt"
if [[ ! -f "$VMLINUZ" || ! -f "$INITRAMFS" ]]; then
  bsdtar -C "$SMOKE_DIR" -xf "$SMOKE_DIR/$ALPINE_ISO" boot/vmlinuz-virt boot/initramfs-virt \
    2>/dev/null && log "extract: vmlinuz-virt + initramfs-virt (direct-boot path)" || true
fi

# ── 4. source tarball + host HTTP server (guest fetches via 10.0.2.2) ───────
log "prep: source tarball -> build/smoke/src.tar.gz"
tar czf "$SMOKE_DIR/src.tar.gz" -C "$ROOT" \
  --exclude=build --exclude=build-asan --exclude=.git --exclude=.claude \
  core host test scripts CMakeLists.txt 2>/dev/null || die "tar failed"

log "prep: host http.server on :$HTTP_PORT (serving $ROOT; guest -> 10.0.2.2:$HTTP_PORT)"
python3 -m http.server "$HTTP_PORT" --directory "$ROOT" >/dev/null 2>&1 &
HTTP_PID=$!

# ── 5. launch QEMU (daemonized; HTTP is the source transport -- no 9p) ──────
QEMU_ARGS=(
  -m 512
  -vga std
  -device qemu-xhci,id=xhci
  -device usb-tablet
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0
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
    -append "console=ttyS0 quiet alpine_dev=cdrom:iso9660 modloop=/boot/modloop-virt modules=loop,squashfs,sd-mod,usb-storage,virtio-pci,virtio_blk"
    -drive "file=$SMOKE_DIR/$ALPINE_ISO,format=raw,if=virtio,media=cdrom,readonly=on"
  )
else
  QEMU_ARGS+=( -drive "file=$SMOKE_DIR/$ALPINE_ISO,format=raw,if=virtio,media=cdrom,readonly=on" )
fi

"$QEMU_BIN" "${QEMU_ARGS[@]}" || die "qemu launch failed"
log "qemu: pid=$(cat "$QEMU_PIDFILE") vnc=$VNC_DISP (TCP 5999) http=:$HTTP_PORT (guest 10.0.2.2:$HTTP_PORT)"

# ── 6. drive the guest over serial (HTTP fetch + native build + run) ────────
cat >&2 <<EOF

[smoke] Optional: VNC viewer to $VNC_DISP (localhost:5999) for the framebuffer.
[smoke] Streaming guest console here now: boot + apk + wget src + build + run...

EOF

log "drive: guest over serial -> HTTP fetch + native build + run fbdev-host"
python3 "$ROOT/scripts/smoke_p1_drive.py" "$SERIAL_SOCK" "$SERIAL_LOG" "$HTTP_PORT" \
  || log "drive.py returned non-zero -- evaluating gates anyway"

# ── 7. screenshot (diagnostic only) ─────────────────────────────────────────
if grep -q 'GUEST_RUN_START' "$SERIAL_LOG" 2>/dev/null && [[ -S "$MONITOR_SOCK" ]]; then
  log "shot: screendump -> $SHOT_PPM"
  printf 'screendump %s\n' "$SHOT_PPM" | socat - UNIX-CONNECT:"$MONITOR_SOCK" >/dev/null 2>&1 || true
fi

# ── 8. gates (deterministic pass/fail from serial markers) ──────────────────
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

# ── 9. verdict ──────────────────────────────────────────────────────────────
if [[ "$ok" -eq 1 ]]; then
  log "PASS -- boot + devices + fbdev-host loop, no crash."
  [[ -f "$SHOT_PPM" ]] && log "  screenshot: $SHOT_PPM (view / convert to png to eyeball)"
  log "  serial log: $SERIAL_LOG"
  exit 0
fi
log "FAIL -- see $SERIAL_LOG; $SHOT_PPM (if present) for the last frame."
exit 1
