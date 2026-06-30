#!/bin/sh
# scripts/guest_smoke_p1.sh -- P1 fbdev-host smoke, runs INSIDE the Alpine guest.
#
# Fetched + invoked by smoke_p1_drive.py over HTTP (host python http.server via
# QEMU user-net 10.0.2.2). It installs the toolchain, wgets the source tarball,
# builds fbdev-host natively, discovers the usb-tablet event node, and runs it.
# All output goes to /dev/ttyS0 so the host can grep GUEST_* markers + the
# "fbdev-host:" lines from the captured serial log.

set -u

exec >/dev/ttyS0 2>&1
echo "GUEST_SCRIPT_START"

HOST=10.0.2.2
PORT=8765

# (a) toolchain (Alpine main+community ship cmake/g++)
setup-apkrepos -1 >/dev/null 2>&1 || true
apk update >/dev/null 2>&1 || { echo "GUEST_FAIL: apk update"; exit 1; }
apk add --quiet cmake g++ make >/dev/null 2>&1 || { echo "GUEST_FAIL: apk add"; exit 1; }

# (b) fetch the source tarball over HTTP (busybox wget is in the base image)
wget -q -O /tmp/src.tar.gz "http://$HOST:$PORT/build/smoke/src.tar.gz" \
  || { echo "GUEST_FAIL: wget src.tar.gz"; exit 1; }
mkdir -p /root/src
tar xzf /tmp/src.tar.gz -C /root/src || { echo "GUEST_FAIL: untar"; exit 1; }

# (c) native build (musl -- no libc-compat pitfall) into guest tmpfs
cd /root/src
cmake -S . -B /tmp/build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
  || { echo "GUEST_FAIL: cmake configure"; exit 1; }
cmake --build /tmp/build -j"$(nproc)" --target fbdev-host >/dev/null 2>&1 \
  || { echo "GUEST_FAIL: cmake build"; exit 1; }
echo "GUEST_BUILD_OK"

# (d) discover the usb-tablet eventN: /proc/bus/input/devices block named
#     "QEMU USB Tablet" reporting EV_ABS (the keyboard has no ABS).
find_event() {
  awk -v RS="" '/QEMU USB Tablet/ && /B: ABS=/ {
      n = $0; sub(/.*Handlers=/, "", n); sub(/\n.*/, "", n)
      split(n, a, " "); for (i in a) if (a[i] ~ /^event/) print a[i]
  }' /proc/bus/input/devices | head -1
}
EVT=$(find_event)
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
