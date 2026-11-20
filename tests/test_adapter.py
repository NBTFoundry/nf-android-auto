# SPDX-License-Identifier: MPL-2.0
"""Drives the adapter as NBT Manager does (ready and lease descriptors,
status descriptor, settings, SIGUSR1, SIGTERM) with the fake daemon of
tests/fake_nbt_aad.py appended to it in place of NBT-AAD's."""

import json
import os
import select
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import unittest

APP = os.environ.get("NF_AA_APP", os.path.join(os.path.dirname(__file__), "..", "build",
                                               "nbt-android-auto-host"))


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def read_byte(fd, timeout):
    ready, _, _ = select.select([fd], [], [], timeout)
    return os.read(fd, 1) if ready else b""


class AdapterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="nf-aa-")
        self.data = os.path.join(self.tmp, "data")
        self.shmem = os.path.join(self.tmp, "shmem")
        os.mkdir(self.data, 0o700)
        os.mkdir(self.shmem)
        self.log = os.path.join(self.tmp, "fake.log")
        self.lease_path = os.path.join(self.tmp, "lease")
        open(self.lease_path, "wb").close()
        self.port = free_udp_port()
        self.fds = []
        self.procs = []

    def tearDown(self):
        for proc in self.procs:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        for fd in self.fds:
            try:
                os.close(fd)
            except OSError:
                pass
        # A daemon the test left behind (the stubborn one, a leftover).
        for record in self.records("start"):
            try:
                os.kill(record["pid"], signal.SIGKILL)
            except OSError:
                pass
        shutil.rmtree(self.tmp)

    def track(self, *fds):
        self.fds.extend(fds)
        return fds if len(fds) > 1 else fds[0]

    def records(self, event=None):
        if not os.path.exists(self.log):
            return []
        with open(self.log) as f:
            out = [json.loads(line) for line in f if line.strip()]
        return [r for r in out if event is None or r["event"] == event]

    def wait_for(self, predicate, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.05)
        return predicate()

    def launch(self, mode="", launch="user", settings=None, extra_env=None):
        ready_r, ready_w = self.track(*os.pipe())
        # The status channel is a file the application rewrites in place.
        self.status_path = os.path.join(self.tmp, "status")
        status_w = self.track(os.open(self.status_path, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o600))
        lease = self.track(os.open(self.lease_path, os.O_RDWR))
        env = {k: v for k, v in os.environ.items() if not k.startswith(("NBT_", "NF_AA_"))}
        env.update({
            "NBT_LAUNCH": launch, "NBT_DATA_DIR": self.data, "NBT_BRIDGE": "1.0",
            "NBT_BRIDGE_CAPS": "status", "NBT_STATUS_FD": str(status_w),
            "NBT_CTL_PORT": str(self.port), "NF_AA_SHMEM_DIR": self.shmem,
            "NF_AA_FAKE": mode, "NF_AA_FAKE_LOG": self.log, "LD_PRELOAD": "",
        })
        for key, value in (settings or {}).items():
            env["NBT_SETTING_" + key.upper()] = value
        env.update(extra_env or {})
        proc = subprocess.Popen(
            [APP, "--ready-fd", str(ready_w), "--lease-fd", str(lease)],
            env=env, pass_fds=(ready_w, lease, status_w),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.procs.append(proc)
        os.close(ready_w)
        os.close(status_w)
        os.close(lease)
        self.fds = [fd for fd in self.fds if fd not in (ready_w, status_w, lease)]
        return proc, ready_r

    def status(self):
        with open(self.status_path) as f:
            return f.read()

    def finish(self, proc, timeout=15.0):
        out, _ = proc.communicate(timeout=timeout)
        return proc.returncode, out.decode(errors="replace")

    def test_check_mode_runs_the_licence_only(self):
        proc, ready_r = self.launch(launch="check")
        self.assertEqual(read_byte(ready_r, 5), b"R")
        self.assertEqual(read_byte(ready_r, 3), b"R")  # heartbeat
        proc.send_signal(signal.SIGTERM)
        code, out = self.finish(proc)
        self.assertEqual(code, 0, out)
        self.assertEqual([r["event"] for r in self.records()], ["license"])
        self.assertEqual(self.records()[0]["argv0"], os.path.join(self.data, "nbt-aad"))

    def test_check_mode_fails_when_the_daemon_does_not_load(self):
        proc, ready_r = self.launch(mode="broken", launch="check")
        code, out = self.finish(proc)
        self.assertEqual(code, 1, out)
        self.assertEqual(read_byte(ready_r, 0.1), b"")

    def test_start_settings_show_status_and_stop(self):
        proc, ready_r = self.launch(
            mode="connected", settings={"audio": "native", "vehicle": "mini", "drive_mode": "on"})
        self.assertEqual(read_byte(ready_r, 5), b"R")
        start = self.records("start")[0]
        self.assertEqual(start["argv0"], os.path.join(self.data, "nbt-aad"))
        self.assertEqual(start["env"]["NBT_AUDIO_MODE"], "browser")
        self.assertEqual(start["env"]["NBT_VEHICLE"], "mini")
        self.assertEqual(start["env"]["NBT_DATA_DIR"], self.data)
        self.assertIn("LD_PRELOAD", start["env"])  # iDrive's preloads reach the daemon
        self.assertIn("show-drivemode", start["files"])
        self.assertTrue(self.wait_for(lambda: "Connected (Wi-Fi)" in self.status(), 4), self.status())
        proc.send_signal(signal.SIGUSR1)
        self.assertTrue(self.wait_for(lambda: {"event": "word", "word": "show"} in self.records()))
        proc.send_signal(signal.SIGTERM)
        code, out = self.finish(proc)
        self.assertEqual(code, 0, out)
        self.assertIn({"event": "word", "word": "quit"}, self.records())
        self.assertEqual(self.records()[-1]["event"], "stop")
        # Its own log, copied into the application log once it ended.
        log = out[out.index("nbt-aad.log ("):]
        self.assertIn("[Main] fake daemon started", log)
        self.assertIn("[Main] word show", log)
        self.assertIn("[Main] fake daemon stopped", log)
        self.assertIn("---- end of", log)

    def test_daemon_gets_no_bridge_descriptors(self):
        proc, ready_r = self.launch()
        self.assertEqual(read_byte(ready_r, 5), b"R")
        pid = self.records("start")[0]["pid"]
        fds = sorted(int(fd) for fd in os.listdir("/proc/%d/fd" % pid))
        # stdin, stdout, stderr and the fake's own socket: nothing of the bridge.
        self.assertEqual(fds[:3], [0, 1, 2])
        self.assertLessEqual(len(fds), 4, fds)
        for fd in (0, 1, 2):  # it keeps its own log
            self.assertEqual(os.readlink("/proc/%d/fd/%d" % (pid, fd)), "/dev/null")
        proc.send_signal(signal.SIGTERM)
        self.finish(proc)

    def test_daemon_crash_ends_the_adapter(self):
        proc, ready_r = self.launch(mode="crash")
        self.assertEqual(read_byte(ready_r, 5), b"R")
        code, out = self.finish(proc)
        self.assertEqual(code, 1, out)
        self.assertIn("daemon ended", out)
        self.assertIn("[Main] fake daemon started", out[out.index("daemon ended"):])

    def test_hung_daemon_is_stopped(self):
        proc, ready_r = self.launch(mode="hang")
        self.assertEqual(read_byte(ready_r, 5), b"R")
        code, out = self.finish(proc, timeout=30)
        self.assertEqual(code, 1, out)
        self.assertIn("stopped answering", out)

    def test_silent_daemon_never_becomes_ready(self):
        proc, ready_r = self.launch(mode="silent")
        code, out = self.finish(proc, timeout=30)
        self.assertEqual(code, 1, out)
        self.assertEqual(read_byte(ready_r, 0.1), b"")
        self.assertIn("did not answer", out)

    def test_stubborn_daemon_is_killed(self):
        proc, ready_r = self.launch(mode="stubborn")
        self.assertEqual(read_byte(ready_r, 5), b"R")
        pid = self.records("start")[0]["pid"]
        proc.send_signal(signal.SIGTERM)
        code, out = self.finish(proc, timeout=30)
        self.assertEqual(code, 0, out)
        self.assertFalse(os.path.exists("/proc/%d" % pid) and
                         open("/proc/%d/stat" % pid).read().split()[2] != "Z")

    def test_another_install_is_left_alone(self):
        other = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        other.bind(("127.0.0.1", self.port))
        self.addCleanup(other.close)
        proc, ready_r = self.launch()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and proc.poll() is None:
            ready, _, _ = select.select([other], [], [], 0.1)
            if ready:
                word, peer = other.recvfrom(64)
                if word == b"ping":
                    other.sendto(b"pong", peer)
        code, out = self.finish(proc)
        self.assertEqual(code, 1, out)
        self.assertIn("another NBT-AAD daemon", out)
        self.assertEqual(self.records("start"), [])

    def test_leftover_daemon_of_ours_is_stopped(self):
        first, ready_r = self.launch()
        self.assertEqual(read_byte(ready_r, 5), b"R")
        first.kill()  # the adapter dies; its daemon stays
        first.wait()
        old_pid = self.records("start")[0]["pid"]
        second, ready_r = self.launch()
        self.assertEqual(read_byte(ready_r, 8), b"R")
        self.assertTrue(self.wait_for(lambda: len(self.records("start")) == 2))
        self.assertNotEqual(self.records("start")[1]["pid"], old_pid)
        second.send_signal(signal.SIGTERM)
        code, out = self.finish(second)
        self.assertEqual(code, 0, out)
        self.assertIn("from an earlier start", out)


if __name__ == "__main__":
    unittest.main()
