#!/usr/bin/env python3
"""smoke_p1_drive.py -- drive the Alpine guest over QEMU's serial unix socket.

Connects to the serial socket, tees everything the guest prints to THIS host
console (and the serial log), waits for boot, logs in, mounts the 9p source
share, and runs the guest smoke script. This replaces manual VNC driving -- the
operator sees the whole boot + apk + build + run streaming on the host console.
VNC remains OPTIONAL, only for eyeballing the framebuffer + wiggling the mouse.

Usage: python3 scripts/smoke_p1_drive.py <serial_sock> <serial_log>
Exit:   0 if the GUEST_RUN_DONE marker was seen, 1 on timeout/error.
"""
import os
import re
import socket
import sys
import threading
import time

SOCK = sys.argv[1]
LOG = sys.argv[2]
DONE = "GUEST_RUN_DONE"
BOOT_TIMEOUT = 180   # seconds to reach a login prompt / shell
STEP_TIMEOUT = 60    # seconds for login / each mount step
RUN_TIMEOUT = 600    # seconds for apk install + cmake build + 40s fbdev-host run


def main():
    # wait for the socket to appear (QEMU just daemonized)
    for _ in range(300):
        if os.path.exists(SOCK):
            break
        time.sleep(0.2)
    else:
        sys.stderr.write("[drive] serial socket never appeared\n")
        return 1

    s = socket.socket(socket.AF_UNIX)
    s.connect(SOCK)
    s.settimeout(0.5)

    buf = []
    log_f = open(LOG, "w", encoding="utf-8")
    lock = threading.Lock()

    def reader():
        while True:
            try:
                data = s.recv(8192)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break
            text = data.decode("utf-8", "replace")
            sys.stdout.write(text)   # stream to host console
            sys.stdout.flush()
            log_f.write(text)        # + capture for the gates
            log_f.flush()
            with lock:
                buf.append(text)

    threading.Thread(target=reader, daemon=True).start()

    def seen(pat):
        with lock:
            return re.search(pat, "".join(buf)) is not None

    def wait_for(pat, timeout, what=""):
        start = time.time()
        while time.time() - start < timeout:
            if seen(pat):
                return True
            time.sleep(0.5)
        sys.stderr.write(f"[drive] timeout waiting for {what or pat!r}\n")
        return False

    def send(cmd):
        sys.stderr.write(f"[drive] >>> {cmd}\n")
        s.sendall((cmd + "\n").encode())

    # 1. boot -> login prompt (or auto-login shell)
    sys.stderr.write("[drive] waiting for guest boot...\n")
    if wait_for(r"login:", BOOT_TIMEOUT, "login prompt"):
        send("root")
        wait_for(r"#", STEP_TIMEOUT, "shell after login")
    elif seen(r"#"):
        sys.stderr.write("[drive] already at a shell (auto-login)\n")
    else:
        sys.stderr.write("[drive] no login prompt and no shell -- boot may have stalled\n")
        wait_for(r"#", STEP_TIMEOUT, "shell")

    # 2. mount the 9p source share. The 9p device (id 0x0009) is attached, the
    # filesystem is registered, and modules are loaded -- so an ENOENT on mount
    # is a tag mismatch. Dump what tag the kernel actually registered (dmesg),
    # then try host0 (what QEMU should set) + a couple of fallbacks, with/without
    # the version= option, so whatever tag QEMU really used we still mount.
    send("mkdir -p /mnt/src")
    time.sleep(1)
    send('modprobe 9pnet_virtio 2>/dev/null; modprobe 9p 2>/dev/null; sleep 2; '
         'echo "=== virtio drivers ==="; '
         'for d in /sys/bus/virtio/devices/virtio*; do '
         'drv=$(readlink $d/driver 2>/dev/null); '
         'echo "$(basename $d) driver=$(basename ${drv:-none}) mod=$(cat $d/modalias)"; '
         'done; '
         'echo "=== dmesg 9p/virtio_9p ==="; '
         'dmesg | grep -iE "9p|vin|virtio_9p" | tail -20; echo DIAG_DONE')
    time.sleep(4)
    send('for t in host0 srcdev; do '
         'for o in "trans=virtio,version=9p2000.L" "trans=virtio"; do '
         'mount -t 9p -o "$o" "$t" /mnt/src 2>/dev/null '
         '&& echo "MOUNTED_AS=$t OPT=$o" && break 2; '
         'done; done; echo MTRY_DONE')
    time.sleep(3)
    send("ls /mnt/src/scripts/guest_smoke_p1.sh && echo MOUNT_OK || echo MOUNT_FAIL")
    time.sleep(2)
    if not seen(r"MOUNT_OK"):
        sys.stderr.write("[drive] 9p mount did not land -- see dmesg + MOUNTED_AS above\n")

    # 3. run the guest smoke script (apk + cmake build + discover event + run)
    send("sh /mnt/src/scripts/guest_smoke_p1.sh")

    # 4. wait for the done marker (apk + build + 40s fbdev-host run)
    ok = wait_for(re.escape(DONE), RUN_TIMEOUT, "guest run done marker")
    sys.stderr.write(f"[drive] {'completed' if ok else 'timed out'}\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
