<!-- SPDX-License-Identifier: MPL-2.0 -->
# Car test: Android Auto under NBT Manager (bridge 1.5 build, status only)

Parked, ignition on. About 45 minutes. Keep SSH/ENET recovery at hand.

This build replaces how NBT-AAD's daemon is installed, started and stopped. It
does not yet replace NBT-AAD's iDrive plugin: keep the hand install's plugin
in place. That plugin finds a daemon by its control port and
`/dev/shmem/nbt-aad-state`, not by who started it (`readDaemonState()` in
`rhmiapp/nbt_aad_app.cpp`). So it should feed the managed daemon the reverse
gear, park assist, camera views, warnings and calls, and hold iDrive's focus
under Android Auto, as it does today. Part B checks that it does.

## What you need

- The car with NBT Manager from a test kit (it trusts the kit's `test-app`
  key) and the phone app paired. Your hand install of NBT-AAD, working.
- `build/car-test/`: `android-auto-1.9.112-seq1.nbt` and `-seq2.nbt`, and
  `SHA256SUMS.txt`. Copy both packages to the phone.
- SSH to the head unit (the logs, and recovery).
- Your NBT-AAD settings (iDrive: Android Auto entry → Settings, and
  `/opt/aa/nbt-aad.env`). The managed daemon reads none of them.

## Before you start

1. In NBT-AAD's Settings, **untick Start automatically**. Then **Force Stop**.
2. Over SSH, check that nothing answers on its port: `pidin -p nbt-aad` lists
   nothing.
3. Do not select NBT-AAD's **Start Android Auto** while no daemon runs: it
   would start the hand install's daemon, and the managed one then refuses to
   start ("Another install runs"). While the managed daemon runs, that entry
   only brings it to the front.

## A. Install, health check, settings

1. On the phone: import `android-auto-1.9.112-seq1.nbt`, send it to the car,
   approve on the car. The manager runs its health check (about 10 s): the
   adapter runs `nbt-aad --license` only. Install must succeed.
2. In the manager's menu, hold the push on **android-auto**: 12 settings.
   Set **Audio**, **Wi-Fi network**, **Over USB**, **Use the phone's GPS**,
   **Car make** and the cluster settings as in your NBT-AAD setup.

## B. Start and use

1. Select **android-auto** in the manager's menu. The manager closes: this
   build has no hand-over of the screen (no `show` grant).
2. Reopen the manager: the entry reads **android-auto (Waiting for phone)**
   (or "No Wi-Fi: USB only" when no network is up).
3. Connect the phone as usual. Android Auto must appear on its own. The label
   becomes **Connected (Wi-Fi)** (or USB).
4. Use it: knob, touch, voice, media, a navigation route, a call, the volume.
5. Through NBT-AAD's plugin (the thing to confirm):
   - Reverse gear: the camera shows, then Android Auto returns.
   - Park assist button; the camera button (Top View / 360).
   - Drive mode or DSC change, if "Show drive mode changes" is on.
   - An incoming call, and an iDrive warning if you have one to trigger.
   - MENU goes to iDrive; NBT-AAD's **Start Android Auto** entry brings Android
     Auto back.

## C. Stop, start again, update

1. In the manager, hold the push on **android-auto**, then **Stop**. Android
   Auto disappears and iDrive's screen is whole. `pidin -p nbt-aad` lists
   nothing.
2. Start it again from the manager. The phone reconnects.
3. While it runs, send `android-auto-1.9.112-seq2.nbt` from the phone and
   approve. The update stops the running copy, runs the health check and
   installs. Start it again.

## D. Over SSH (optional)

The adapter runs as `/var/opt/nbt-manager/apps/android-auto/bin/app`; find
its pid with `pidin arg | grep android-auto/bin/app`. The daemon is `nbt-aad`,
from `/var/opt/nbt-manager/data/android-auto/nbt-aad`.

- `kill <adapter pid>` (SIGTERM): a clean stop as in C.1.
- `kill -9 <adapter pid>`: its daemon keeps running. Start from the manager
  again: the adapter must stop that leftover daemon first ("a daemon from an
  earlier start still runs") and then start.
- Do not SIGKILL `nbt-aad` itself: nothing restores the screen after that yet
  (seam S3). If you did: `LayerManagerControl set surface 256 visibility 1`.

## Logs (before switching the car off: `/dev/shmem` is RAM)

- The adapter's lines: the phone app's log for **android-auto**.
- The daemon: `/dev/shmem/nbt-aad.log` (5 MB, per start). For a log that
  survives power off, before starting it:
  `touch /var/opt/nbt-manager/data/android-auto/usblog` with a USB stick
  in. The daemon then writes `nbt-aad.log` on the stick, as with NBT-AAD's
  "Save logs to USB". Remove the file afterwards.
- NBT-AAD's plugin: `/dev/shmem/nbt-aad-app.log`.
- The manager: its debug log, with the debug log on.

## Back to the hand install

Uninstall **android-auto** from the manager (hold → Uninstall, or from the
phone). Tick NBT-AAD's **Start automatically** again.

## Report

| Step | Result | Notes |
| --- | --- | --- |
| A.1 install + health check | | |
| A.2 settings shown and kept | | |
| B.1-3 start, label, phone connects | | |
| B.4 use (knob, touch, audio, nav, call, volume) | | |
| B.5 reverse / park assist / camera / warnings / calls | | |
| C.1 stop, screen whole | | |
| C.2 start again | | |
| C.3 update while running | | |
| D leftover daemon stopped | | |

With the car's software version (Navigation → Settings → Position and
software version) and the phone model.
