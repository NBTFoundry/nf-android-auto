#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A stand-in for NBT-AAD's daemon in host tests: the parts of its interface
the adapter uses (src/protocol.hpp), driven by the environment.

- `--license`: prints a line and exits 0 (1 with NF_AA_FAKE=broken).
- Otherwise it claims the control port (NBT_CTL_PORT, default 52790) and
  answers "ping" with "pong"; writes its state and status records in
  NF_AA_SHMEM_DIR; appends each word it receives, and once its environment
  and the files in NBT_DATA_DIR, to NF_AA_FAKE_LOG as JSON lines; ends on
  "quit" or SIGTERM.
- NF_AA_FAKE modes: silent (never answers), crash (exits 3 after 1 s), hang
  (stops answering after 1 s), stubborn (ignores "quit" and SIGTERM),
  connected (reports a Wi-Fi session).
- Its own log, nbt-aad.log in NF_AA_SHMEM_DIR, fresh at each start: a start
  line, each word received, a stop line.
"""
import json
import os
import signal
import socket
import sys
import time

MODE = os.environ.get("NF_AA_FAKE", "")
LOG = os.environ.get("NF_AA_FAKE_LOG")
SHMEM = os.environ.get("NF_AA_SHMEM_DIR", "/dev/shm")


def log(**record):
    if LOG:
        with open(LOG, "a") as f:
            f.write(json.dumps(record) + "\n")


def daemon_log(line, fresh=False):
    with open(os.path.join(SHMEM, "nbt-aad.log"), "w" if fresh else "a") as f:
        f.write(line + "\n")


def write_records(state, status):
    with open(os.path.join(SHMEM, "nbt-aad-state"), "w") as f:
        f.write("%-11.11s %10d %d %d %u\n" % (state, os.getpid(), 0, 0, 0))
    with open(os.path.join(SHMEM, "nbt-aad-status"), "w") as f:
        f.write(status)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--license":
        print("fake licence")
        log(event="license", argv0=sys.argv[0])
        return 1 if MODE == "broken" else 0
    running = [True]

    def on_term(signum, frame):
        if MODE != "stubborn":
            running[0] = False

    signal.signal(signal.SIGTERM, on_term)
    if MODE == "stubborn":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    port = int(os.environ.get("NBT_CTL_PORT", "52790"))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(0.1)
    data_dir = os.environ.get("NBT_DATA_DIR", "")
    files = sorted(os.listdir(data_dir)) if os.path.isdir(data_dir) else []
    log(event="start", pid=os.getpid(), argv0=sys.argv[0], files=files,
        env={k: v for k, v in os.environ.items() if k.startswith(("NBT_", "LD_"))})
    daemon_log("[Main] fake daemon started, pid %d" % os.getpid(), fresh=True)
    if MODE == "connected":
        write_records("connected", "connected|1|0|1|wifi")
    else:
        write_records("idle", "waiting-phone|1|0|1|-")
    started = time.monotonic()
    while running[0]:
        if MODE == "crash" and time.monotonic() - started > 1.0:
            return 3
        try:
            word, peer = sock.recvfrom(512)
        except socket.timeout:
            continue
        word = word.decode(errors="replace")
        log(event="word", word=word)
        daemon_log("[Main] word %s" % word)
        if word == "ping":
            silent = MODE == "silent" or (MODE == "hang" and time.monotonic() - started > 1.0)
            if not silent:
                sock.sendto(b"pong", peer)
        elif word == "quit" and MODE != "stubborn":
            running[0] = False
    log(event="stop")
    daemon_log("[Main] fake daemon stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
