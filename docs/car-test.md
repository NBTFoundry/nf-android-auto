<!-- SPDX-License-Identifier: MPL-2.0 -->
# Car test: Android Auto under NBT Manager (bridge 1.6)

Parked, ignition on. About 45 minutes. Keep SSH/ENET recovery at hand.

This build does what NBT-AAD's own iDrive plugin did, through NBT Manager's
bridge 1.6: it holds iDrive on a blank state under Android Auto (the knob and
the favourite keys stop reaching iDrive's menus underneath), steps aside for
the reverse camera, park assist, camera views and screens the car opens,
keeps Android Auto during calls, feeds the daemon its car data, and has the
manager open TCP 5277 in the packet filter (docs/upstream-interface.md,
"Bridge 1.6").

## What you need

- NBT Manager from test kit **dev122** (bridge 1.6), with its phone app,
  paired. Update the manager from the phone, then **Restart iDrive** (the
  plugin changed).
- **No NBT-AAD hand install**: its plugin (`libNbtAad.so`) would hold iDrive
  too. If it is still there, uninstall it first.
- `android-auto-0.0.6.nbt` and `android-auto-0.0.7.nbt` (NBT-AAD 1.9.112
  inside both) and `SHA256SUMS.txt`, from the kit folder. Copy both packages
  to the phone.
- SSH to the head unit (the logs, and recovery).

## A. Install, settings

1. On the phone: send `android-auto-0.0.6.nbt` to the car, approve on the car.
   The update replaces 0.0.5 and runs the health check (about 10 s).
2. In the manager's menu, hold the push on **android-auto**: the 12 settings
   are kept from 0.0.5. Set **Show drive mode changes: on** for B.6.

## B. Start and use

1. Select **android-auto** in the manager's menu: the manager stays open and
   says "Application started"; connect the phone (Wi-Fi Direct, the default).
   Android Auto appears; the label reads **Connected (Wi-Fi)**.
2. **Knob**: turn and push. Only Android Auto reacts; iDrive's menu underneath
   does not move (no click, no strip of iDrive at the edges).
3. **Favourite keys**: touch one (iDrive's favourites bar appears over Android
   Auto, as with CarPlay, without glitches), let go: the bar disappears and
   the knob still drives Android Auto. Press one: what does it do?
4. **MENU**, **NAV**, **MEDIA**, **COM**: each goes to Android Auto (its own
   screen) and iDrive stays held. **BACK** short: Android Auto's back; BACK
   held: Android Auto minimizes to iDrive.
5. **Reverse gear**: the camera shows; back to drive: Android Auto returns by
   itself. The **park assist** button and the **camera** button (Top View /
   360): Android Auto steps aside, and returns when the view closes.
6. **Drive mode** rocker and **DSC** button: iDrive's display shows for a few
   seconds, then Android Auto returns.
7. **A call** (incoming or from Android Auto): Android Auto stays; after the
   call the knob still drives Android Auto.
8. A screen the car opens on its own (a Check Control message, the safety
   button): Android Auto steps aside and stays aside; the manager's
   **android-auto** entry (or the phone's "show") brings it back.
9. Day/night: drive into a garage or wait for dusk if you can; Android Auto's
   night mode follows the car.

## C. Stop, start again, update, Wi-Fi Direct

1. In the manager, hold the push on **android-auto**, then **Stop**. Android
   Auto disappears, iDrive's screen is whole, the knob drives iDrive again.
2. Start it again; the phone reconnects over Wi-Fi Direct. Stop and start
   twice more: it must reconnect each time (before, it stayed on "Waiting for
   phone").
3. While it runs, send `android-auto-0.0.7.nbt` and approve: the update stops
   it, checks and installs. Start it again.

## D. Over SSH (optional)

- The packet filter rule while it runs: `pfctl -s rules | grep 5277` lists
  `nbt-app-android-auto` rules on `uap0` and `wfd0`; after a stop it lists none.
- `pidin arg | grep android-auto/bin/app`: `kill <pid>` stops it cleanly, as C.1.

## Logs (before switching the car off: `/dev/shmem` is RAM)

- The phone app: **android-auto** → **Show log** → **Share**, after stopping
  it (the daemon's own log is copied in when it ends). Lines `bridge: ...`
  show each hold request, car value and step aside.
- NBT Manager's debug log, with the debug log on before the test (the
  plugin's `application hold` and `car data` lines).
- `/dev/shmem/nbt-aad.log` if it ended badly.

## Back to 0.0.5

Send `android-auto-0.0.5.nbt` again (or uninstall from the manager).

## Report

| Step | Result | Notes |
| --- | --- | --- |
| A.1 update to 0.0.6 + health check | | |
| A.2 settings kept | | |
| B.1 start, manager stays, phone connects | | |
| B.2 knob only drives Android Auto | | |
| B.3 favourites bar over Android Auto; pressing a key | | |
| B.4 MENU/NAV/MEDIA/COM, BACK short/held | | |
| B.5 reverse, park assist, camera button | | |
| B.6 drive mode, DSC | | |
| B.7 call | | |
| B.8 car screen: aside, back from the manager | | |
| B.9 day/night | | |
| C.1 stop, iDrive whole | | |
| C.2 start/stop ×3 over Wi-Fi Direct | | |
| C.3 update to 0.0.7 while running | | |
| D packet filter rules come and go | | |
