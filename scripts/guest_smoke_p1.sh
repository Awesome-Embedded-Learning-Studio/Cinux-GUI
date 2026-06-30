#!/bin/sh
# scripts/guest_smoke_p1.sh -- runs INSIDE the Alpine guest (P1 fbdev-host smoke)
#
# You invoke this from the guest's VNC shell:
#     sh /mnt/src/scripts/guest_smoke_p1.sh
#
# All stdout/stderr is redirected to /dev/ttyS0 so the host (smoke_p1.sh) can
# grep the GUEST_* markers + "fbdev-host:" lines from the captured serial log.
# fbdev-host's own framebuffer output shows up on the VNC display for the human
# to see. Pass/fail is grepped from serial; the picture is for eyeballing.

set -u

exec >/dev/ttyS0 2>&1
echo "GUEST_SCRIPT_START"

# (a) mount the host source tree via the read-only 9p share (tag host0)
mkdir -p /mnt/src
mount -t 9p -o trans=virtio,version=9p2000.L,ro host0 /mnt/src \
  || { echo "GUEST_FAIL: 9p mount host0"; exit 1; }

# (b) toolchain (Alpine main+community ship cmake/g++)
setup-apkrepos -1 >/dev/null 2>&1 || true
apk update >/dev/null 2>&1 || { echo "GUEST_FAIL: apk update"; exit 1; }
apk add --quiet cmake g++ make >/dev/null 2>&1 || { echo "GUEST_FAIL: apk add"; exit 1; }

# (c) native build (musl -- no libc-compat pitfall) into guest tmpfs
cd /mnt/src
cmake -S . -B /tmp/build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
  || { echo "GUEST_FAIL: cmake configure"; exit 1; }
cmake --build /tmp/build -j"$(nproc)" --target fbdev-host >/dev/null 2>&1 \
  || { echo "GUEST_FAIL: cmake build"; exit 1; }
echo "GUEST_BUILD_OK"

# (d) discover the usb-tablet eventN: parse /proc/bus/input/devices for the block
#     named "QEMU USB Tablet" that also reports EV_ABS (the keyboard has no ABS).
find_event() {
  awk -v RS="" '/QEMU USB Tablet/ && /B: ABS=/ {
      n = $0; sub(/.*Handlers=/, "", n); sub(/\n.*/, "", n)
      split(n, a, " "); for (i in a) if (a[i] ~ /^event/) print a[i]
  }' /proc/bus/input/devices | head -1
}
EVT=$(find_event)
# fallback: any block with B: ABS= (only the tablet reports EV_ABS here)
[ -z "$EVT" ] && EVT=$(awk -v RS="" '/B: ABS=/ {
      n = $0; sub(/.*Handlers=/, "", n); sub(/\n.*/, "", n)
      split(n, a, " "); for (i in a) if (a[i] ~ /^event/) print a[i]
  }' /proc/bus/input/devices | head -1)
echo "GUEST_TABLET_EVENT=${EVT:-none}"

# (e) device presence gates
if [ -c /dev/fb0 ]; then
  echo "GUEST_FB0_OK name=$(cat /sys/class/graphics/fb0/name 2>/dev/null || echo '?')"
else
  echo "GUEST_FAIL: no /dev/fb0"
fi
if [ -n "$EVT" ] && [ -c "/dev/input/$EVT" ]; then
  echo "GUEST_TABLET_OK"
else
  echo "GUEST_FAIL: no tablet event node"
fi

# (f) RUN fbdev-host under timeout. argv[1]=fb path, argv[2]=event path.
echo "GUEST_RUN_START"
timeout 40 /tmp/build/fbdev-host /dev/fb0 "/dev/input/$EVT"
echo "GUEST_RUN_RC=$?"   # 124 = timeout killed it (expected/good)
echo "GUEST_RUN_DONE"
