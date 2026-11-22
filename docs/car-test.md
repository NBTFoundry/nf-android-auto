<!-- SPDX-License-Identifier: MPL-2.0 -->
# Car test: Android Auto before a release

Parked, ignition on. About 45 minutes, all from the phone: no adapter, no
computer.

Run it on a candidate before tagging a release, after moving the NBT-AAD pin,
and whenever the car-side behaviour changed. Through NBT Manager's bridge 1.6
the adapter does what NBT-AAD's own iDrive plugin did: it holds iDrive on a
blank state under Android Auto (the knob and the favourite keys stop reaching
iDrive's menus underneath), steps aside for the reverse camera, park assist,
camera views and screens the car opens, keeps Android Auto during calls, feeds
the daemon its car data, and has the manager open TCP 5277 in the packet
filter (docs/upstream-interface.md, "Bridge 1.6").

## What you need

- NBT Manager 0.3.1 or later on the car, and its phone app, paired.
- **No NBT-AAD hand install**: its plugin (`libNbtAad.so`) would hold iDrive
  too. If it is still there, uninstall it first.
- From the kit folder: the candidate `android-auto-<version>.nbt`, a second one
  with a higher sequence for part D, and their `SHA256SUMS` file. Copy them to
  the phone.

## A. Install, settings

| # | Do | Look for |
| --- | --- | --- |
| A1 | Library: add the candidate, open it | Under the file name: **NBT-AAD** (the version it carries), **Project: NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)**, **Licence: PolyForm Noncommercial 1.0.0**, then two paragraphs (by NBT-AAD with NBTFoundry's changes; the Required Notice) |
| A2 | Car › android-auto › install or **Update**; approve on the car | It installs; the health check passes (about 10 s) |
| A3 | Hold the push on **android-auto** | Its 12 settings (kept by an update); set **Show drive mode changes: on** for B6 |

## B. Start and use

| # | Do | Look for |
| --- | --- | --- |
| B1 | Select **android-auto**; connect the phone (Wi-Fi Direct, the default) | The manager stays open ("Application started"); Android Auto appears, label **Connected (Wi-Fi)** |
| B2 | Maps, then media | Video smooth, sound |
| B3 | Knob: turn and push | Only Android Auto reacts; iDrive's menu underneath does not move |
| B4 | Touch a favourite key, let go; then press one | The favourites bar shows over Android Auto and goes; the knob still drives Android Auto |
| B5 | MENU, NAV, MEDIA, COM; BACK short, then held | Each goes to Android Auto's own screen; BACK short is Android Auto's back, held minimizes to iDrive |
| B6 | Reverse gear, then drive; park assist; the camera button | The camera shows; Android Auto steps aside and comes back by itself |
| B7 | Drive mode rocker, DSC button | iDrive's display for a few seconds, then Android Auto |
| B8 | A call (incoming or from Android Auto) | Android Auto stays; after the call the knob still drives it |
| B9 | A screen the car opens (a Check Control message) | Android Auto steps aside; the manager's **android-auto** entry brings it back |

## C. NBT-AAD's own features

Settings first: **Audio** `native`, **LCI wheel media keys** `on`, then start it
again. The volume floor of 25 in native audio is a known NBT-AAD limit.

| # | Do | Look for |
| --- | --- | --- |
| C1 | Music playing, a route in Maps; wait for a prompt | The prompt is heard, the music muted under it, then back |
| C2 | Turn the volume during the prompt, then during music | The bar says Navigation, then Entertainment; each keeps its own level |
| C3 | Head unit's previous / next buttons | The track changes in Android Auto |
| C4 | Hold the wheel's voice button; then a short press | Google Assistant; the short press gives iDrive's voice |

## D. Stop, start again, update

| # | Do | Look for |
| --- | --- | --- |
| D1 | Hold the push on **android-auto** › **Stop** | Android Auto goes away, iDrive is whole, the knob drives iDrive |
| D2 | Start it again, twice | The phone reconnects over Wi-Fi Direct each time |
| D3 | While it runs, send the second package; approve | It stops, checks, installs; start it again: it connects |

## Logs (before switching off: `/dev/shmem` is RAM)

- Phone app: **android-auto** › **Show log** › **Share**, after stopping it
  (the daemon's own log is copied in when it ends). Lines `bridge: ...` show
  each hold request, car value and step aside.
- If anything failed: the car's page › **iDrive log** › **Share**, or the
  phone's **Collect everything** (Install NBT Manager on a car › More tools,
  over the Wi-Fi ENET adapter).

## Back

Uninstall **android-auto** in the manager, or install the last release from
GitHub (a test package with a lower sequence is refused).

## Results

| Step | OK / not OK | Notes |
| --- | --- | --- |
| A1 information shown | | |
| A2-A3 install, settings | | |
| B1-B2 start, connect, video and sound | | |
| B3-B5 knob, favourites, hard keys | | |
| B6-B7 reverse, park assist, camera, drive mode, DSC | | |
| B8-B9 call, car screen | | |
| C1-C2 navigation prompts and volumes | | |
| C3-C4 media buttons, voice button | | |
| D1-D3 stop, reconnect, update | | |

With the car's software version (Navigation → Settings → Position and
software version) and the phone model.
