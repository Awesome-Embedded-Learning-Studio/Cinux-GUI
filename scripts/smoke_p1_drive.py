#!/usr/bin/env python3
"""smoke_p1_drive.py -- drive the Alpine guest over QEMU's serial unix socket.

Streams the guest console to the host, logs in, fetches the guest smoke script
over HTTP (host python http.server via QEMU user-net 10.0.2.2), and runs it.
The guest script does apk + wget source tarball + native cmake build + run
fbdev-host. 9p proved unreliable (driver bound but tag never matched); HTTP is
the source transport. VNC is OPTIONAL (framebuffer eyeballing + mouse only).

Usage: python3 scripts/smoke_p1_drive.py <serial_sock> <serial_log> <http_port>
Exit:   0 if GUEST_RUN_DONE was seen, 1 on timeout/error.
"""
import os
import re
import socket
import sys
import threading
import time

SOCK = sys.argv[1]
LOG = sys.argv[2]
HTTP_PORT = sys.argv[3] if len(sys.argv) > 3 else "8765"
DONE = "GUEST_RUN_DONE"
BOOT_TIMEOUT = 180
STEP_TIMEOUT = 60
RUN_TIMEOUT = 900   # apk update + apk add + cmake build on a cold guest can be slow


def main():
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
            sys.stdout.write(text)
            sys.stdout.flush()
            log_f.write(text)
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
        wait_for(r"#", STEP_TIMEOUT, "shell")

    # 2. fetch the guest script over HTTP (QEMU user net: 10.0.2.2 = host)
    send(f"wget -O /tmp/g.sh http://10.0.2.2:{HTTP_PORT}/scripts/guest_smoke_p1.sh 2>&1 | tail -2; echo GS=$?")
    if not wait_for(r"GS=0", STEP_TIMEOUT, "fetch guest script (GS=0)"):
        sys.stderr.write(f"[drive] guest script fetch failed -- host http.server on :{HTTP_PORT}? "
                         f"QEMU -netdev user present?\n")
        return 1

    # 3. run the guest script (apk + wget source + build + run fbdev-host)
    send("sh /tmp/g.sh")
    ok = wait_for(re.escape(DONE), RUN_TIMEOUT, "guest run done marker")
    sys.stderr.write(f"[drive] {'completed' if ok else 'timed out'}\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
