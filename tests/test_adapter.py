# SPDX-License-Identifier: MPL-2.0
"""Drives the adapter as NBT Manager does (ready and lease descriptors,
status descriptor, settings, SIGUSR1, SIGTERM) with the fake daemon of
tests/fake_nbt_aad.py appended to it in place of NBT-AAD's."""

import json
import os
import re
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

    def launch(self, mode="", launch="user", settings=None, extra_env=None, extra_fds=()):
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
            env=env, pass_fds=(ready_w, lease, status_w) + tuple(extra_fds),
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

    # ---- bridge 1.6 (src/daemon_bridge.cpp) ----

    def hold_request(self):
        """The adapter's last hold request: (sequence, word), or None."""
        with open(self.hold_path) as f:
            text = f.read()
        if not text:
            return None
        sequence, word = text.split()
        return int(sequence), word

    def car(self, name, sequence, value):
        with open(os.path.join(self.shmem, "nbt-bridge1-cds-" + name), "w") as f:
            f.write("%d %s\n" % (sequence, value))

    def answer(self, sequence, word):
        with open(self.answer_path, "w") as f:
            f.write("%d %s\n" % (sequence, word))

    def fake(self, overlay, preview=0):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.sendto(b"fake-overlay %d %d" % (overlay, preview), ("127.0.0.1", self.port))

    def words(self, prefix=""):
        return [r["word"] for r in self.records("word")
                if r["word"].startswith(prefix) and not r["word"].startswith(("ping", "fake-"))]

    def test_bridge_16_hold_car_data_and_network(self):
        self.hold_path = os.path.join(self.shmem, "nbt-bridge1-android-auto.hold")
        self.answer_path = os.path.join(self.shmem, "nbt-bridge1-android-auto.hmi")
        hold_fd = self.track(os.open(self.hold_path, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o600))
        proc, ready_r = self.launch(mode="connected", extra_fds=(hold_fd,), extra_env={
            "NBT_BRIDGE": "1.6", "NBT_BRIDGE_CAPS": "status,hold,car-data,net",
            "NBT_HOLD_FD": str(hold_fd), "NBT_HOLD_STATE": self.answer_path,
            "NBT_CDS_DIR": self.shmem, "NBT_NET": "tcp:5277@uap0+wfd0"})
        os.close(hold_fd)
        self.fds.remove(hold_fd)
        self.assertEqual(read_byte(ready_r, 5), b"R")
        # The manager opened the port: the daemon leaves the packet filter on.
        self.assertEqual(self.records("start")[0]["env"]["NBT_KEEP_PF"], "1")

        # Car data as the plugin sent it, and the start values for a daemon.
        self.car("driving.gear", 1, '{"gear":2}')
        self.car("vehicle.dayNightChangeOver", 1, '{"dayNightChangeOver":2}')
        self.assertTrue(self.wait_for(lambda: "gear 2" in self.words() and "daynight 2" in self.words()))
        with open(os.path.join(self.shmem, "nbt-aad-gear")) as f:
            self.assertEqual(f.read(), "gear 2\n")
        self.car("driving.gear", 2, '{"gear":1}')
        self.car("hmi.graphicalContext", 1, '{"graphicalContext":{"visibleContext":692}}')
        self.assertTrue(self.wait_for(lambda: "gear 1" in self.words() and "camera 692" in self.words()))
        self.car("hmi.graphicalContext", 2, '{"graphicalContext":{"visibleContext":13}}')
        self.car("driving.mode35up", 1, '{"mode":4}')
        self.assertTrue(self.wait_for(lambda: "camera 0" in self.words()
                                      and "drivemode 1 4 -1" in self.words()))

        # The hold follows Android Auto on screen, the favourites bar, and leaving.
        self.assertIsNone(self.hold_request())
        self.fake(1)
        self.assertTrue(self.wait_for(lambda: (self.hold_request() or (0, ""))[1] == "hold"))
        self.fake(1, preview=1)
        self.assertTrue(self.wait_for(lambda: self.hold_request()[1] == "peek"))
        self.fake(1)
        self.assertTrue(self.wait_for(lambda: self.hold_request()[1] == "hold"))
        # iDrive opened a screen of its own: the daemon steps aside.
        self.answer(100, "aside")
        self.assertTrue(self.wait_for(lambda: "hide" in self.words()))
        # A key that went to the phone: iDrive is held again instead.
        before = self.hold_request()[0]
        self.fake(3)
        time.sleep(0.3)
        self.answer(101, "aside")
        self.assertTrue(self.wait_for(lambda: self.hold_request()[0] > before))
        self.assertEqual(self.hold_request()[1], "hold")
        self.assertEqual(self.words("hide"), ["hide"])
        # A call: Android Auto stays, held again when the call ends.
        self.fake(1)
        self.car("communication.callStatus0", 1, '{"callStatus":{"id":0,"state":1}}')
        time.sleep(0.5)
        before = self.hold_request()[0]
        self.answer(102, "aside")
        time.sleep(0.5)
        self.assertEqual(self.hold_request()[0], before)
        self.car("communication.callStatus0", 2, '{"callStatus":{"id":0,"state":0}}')
        self.assertTrue(self.wait_for(lambda: self.hold_request()[0] > before))
        self.assertEqual(self.words("hide"), ["hide"])
        # iDrive's driving-mode screen comes before the new mode (dev122): the
        # adapter waits for the car data, then holds iDrive again.
        self.fake(1)
        time.sleep(0.3)
        before = self.hold_request()[0]
        self.answer(103, "aside")
        time.sleep(0.4)
        self.car("driving.mode35up", 2, '{"mode":9}')
        self.assertTrue(self.wait_for(lambda: self.hold_request()[0] > before))
        self.assertEqual(self.hold_request()[1], "hold")
        self.assertIn("drivemode 1 9 4", self.words())
        time.sleep(1.5)
        self.assertEqual(self.words("hide"), ["hide"])
        # Android Auto left the screen.
        self.fake(0)
        self.assertTrue(self.wait_for(lambda: self.hold_request()[1] == "release"))

        proc.send_signal(signal.SIGTERM)
        code, out = self.finish(proc)
        self.assertEqual(code, 0, out)
        self.assertIn("bridge: focus hold on, car data on, network rules tcp:5277@uap0+wfd0", out)

    def test_bridge_15_manager_gets_nothing_new(self):
        proc, ready_r = self.launch(mode="connected")
        self.assertEqual(read_byte(ready_r, 5), b"R")
        self.assertNotIn("NBT_KEEP_PF", self.records("start")[0]["env"])
        proc.send_signal(signal.SIGTERM)
        code, out = self.finish(proc)
        self.assertEqual(code, 0, out)
        self.assertIn("bridge: focus hold off, car data off, network rules none", out)

    def test_package_declares_the_car_data_it_reads(self):
        root = os.path.join(os.path.dirname(__file__), "..")
        with open(os.path.join(root, "src", "protocol.hpp")) as f:
            names = re.findall(r'^const char kCds\w+\[\] = "([^"]+)";', f.read(), re.M)
        with open(os.path.join(root, "release", "package.args")) as f:
            lines = f.read().split("\n")
        self.assertEqual(lines[lines.index("--bridge-cds") + 1], ",".join(names))
        self.assertEqual(lines[lines.index("--bridge-net") + 1], "tcp:5277@uap0+wfd0")


if __name__ == "__main__":
    unittest.main()
