<!-- SPDX-License-Identifier: MPL-2.0 -->
# NBT-AAD's daemon, as the adapter sees it

NBT-AAD ships two programs: the daemon (`nbt-aad`, Android Auto itself) and an
iDrive plugin (`libNbtAad.so`) that starts it, feeds it car data and holds
iDrive's focus. Under NBT Manager the plugin is retired: the manager's plugin
and bridge take its place, and this repository's adapter stands between the
manager and the unmodified daemon. This page lists everything the adapter
relies on, at the pin (`third_party/nbt-aad`, v1.9.112 plus the seams below).

## How the facts are kept

Each fact is written once, in [src/protocol.hpp](../src/protocol.hpp) (words,
files, the port) or [src/settings.def](../src/settings.def) (settings).
`make check-protocol`, part of `make test`, finds every one of them in the
pinned sources: moving the pin to a release that renamed one fails the build.
The adapter ignores what it does not know (a state it has no label for, a
newer field), so a newer daemon behind this adapter loses features, never
the start.

## Start and stop

| What | How |
| --- | --- |
| Program | Appended to the adapter (`nbt/app/payload.h`), copied to `NBT_DATA_DIR/nbt-aad` at every start |
| Folders | `NBT_DATA_DIR` makes it a managed install (seam S1): its settings flags, `nbt-aad.env` and boot guard live there; nothing under `/opt/aa` or `/var/opt/aa` is read |
| Environment | The manager's (iDrive's `PATH`, `LD_LIBRARY_PATH`, `LD_PRELOAD`, `GRAPHICS_ROOT`, `TZ`), the settings' `NBT_*` variables first; the manager's audio connection (`NBT_AUDIO_CONN`, its `native_audio` setting) as `NBT_BROWSER_CONN`, the connection the daemon's native audio builds |
| Descriptors | `/dev/null` for all three, nothing of the bridge: the daemon then keeps its own log, 5 MB in `/dev/shmem/nbt-aad.log` (or on a USB stick with the flag `usblog` in the data folder); the manager's 64 KiB application log holds the adapter's lines and, once the daemon ends, the first 8 KiB and last 16 KiB of its RAM log (the Wi-Fi source and Bluetooth set-up, then its last lines) |
| Health check | `nbt-aad --license`: the program and the car's libraries it links load; nothing else is touched |
| Ready | The first `pong` on the control port, within 4.5 s |
| Alive | A `pong` every 2 s; three missed answers stop it as hung |
| Stop | `quit`, then SIGTERM after 3 s and SIGKILL after 10 s (iDrive's Force Stop) |
| Another daemon | One answering on the port whose state record names a process running our copy is a leftover and is stopped; any other (a hand or NBTApps install) is left alone and the start fails with "Another install runs" |

## Control port

UDP `127.0.0.1:52790` (`NBT_CTL_PORT` moves it; host tests only). Words the
adapter sends: `ping` (answered `pong`), `show` (the manager's
bring-to-front), `quit`, and what the retired plugin sent, from bridge 1.6
(below): `gear <n>`, `pdc <n>`, `daynight <n>`, `drivemode <prop> <n> <prev>`,
`dsc <n> <prev>`, `camera <ctx>`, `hide`. The last gear, park-assist and
day/night words also go to `nbt-aad-gear`, `nbt-aad-pdc` and
`nbt-aad-daynight`, which a daemon reads at its start.

## Records in `/dev/shmem`

| File | Content | Used for |
| --- | --- | --- |
| `nbt-aad-state` | `<state> <pid> <overlay> <preview> <mask>` | which process is the daemon; `overlay` 1-5 while Android Auto is on screen (the focus hold, bridge 1.6) |
| `nbt-aad-status` | `<waiting-phone\|connecting\|connected>\|<carplay>\|<wfd>\|<usb>\|<wifi\|usb\|->` | the menu's status label |
| `nbt-aad-surface`, `nbt-aad-lm1280` | the LayerManager surface and saved geometry | clean-up after a SIGKILL (seam S3) |

## Settings

The manager shows twelve choices (`src/settings.def`); each becomes an
`NBT_*` variable, which wins over NBT-AAD's flag files, or a flag file in the
data folder where the daemon has no variable. Not offered: NBT-AAD's options
for developers (logging, probes, timings), its "Prompts on car radio (BT)"
beta (the manager takes at most twelve settings), and the plugin's own
checkboxes (autostart, USB logs, MENU opens iDrive, iDrive warnings), which
the manager provides or the adapter decides (bridge 1.6, below).

## Seams (upstream pull requests from `NBTFoundry/Supra-iDrive6-Android-Auto`)

| Seam | State | What |
| --- | --- | --- |
| S1 | branch `managed-install` (pinned) | `NBT_DATA_DIR` is a managed install's only folder; the favourites bar, picker and vehicle files go through `nbt_paths.hpp` |
| S2 | to do | one header naming the control words and records, which the adapter includes instead of `src/protocol.hpp`'s copies (with NBT-AAD's draft #104, `car_state.hpp`) |
| S3 | to do | `nbt-aad --recover-screen`: the plugin's clean-up after a SIGKILL, in the daemon, for the adapter to run |
| S4 | to do | a start mode for the health check: the control port without Bluetooth, Wi-Fi, firewall or screen |

## Bridge 1.6: what the plugin did

The package declares bridge 1.6 with `hold`, `car-data` and `net` optional
(`release/package.base`): a manager without them still runs it, with
`status` only. [src/daemon_bridge.cpp](../src/daemon_bridge.cpp) follows the
plugin's rules (NBT-AAD's `rhmiapp/nbt_aad_app.cpp`), with its own parsers
(`rhmiapp/cds_parse.hpp`).

- **Focus hold** (`hold`): `hold` while the state record's overlay says
  Android Auto is on screen, `peek` while iDrive's favourites bar is
  previewed over it (iDrive keeps the state, nothing is re-asserted),
  `release` when it leaves; unchanged while BACK is held (overlay 2: the daemon
  may minimize). When the manager answers `aside` (iDrive left the blank
  state): during a call Android Auto stays and iDrive is held again when the
  call ends; reverse, park assist and a camera view are the daemon's own
  (nothing); a key, a car control or the knob that went to the phone in the
  last 3 s (overlay 3-5) or a driving-mode or DSC change in the last 5 s holds
  iDrive again; otherwise the car opened a screen of its own (camera button,
  safety, Check Control) and the daemon gets `hide`, until the driver chooses
  Android Auto again.
- **Car data** (`bridge-cds`): `driving.gear`, `sensors.PDCStatus`,
  `vehicle.dayNightChangeOver`, `driving.mode`, `driving.mode35up`,
  `driving.DSCActive`, `hmi.graphicalContext` (camera views 692 and 1023),
  `communication.callStatus0` and `1`, read every 250 ms; each change becomes
  the word above, as the plugin sent it.
- **Firewall** (`bridge-net=tcp:5277@uap0+wfd0`): the manager opens TCP 5277
  on the CarPlay hotspot and the Wi-Fi Direct group for the run (and keeps the
  group on); the daemon gets `NBT_KEEP_PF=1` and leaves the filter on.

Not carried over: the plugin's "Show iDrive warnings" (Android Auto coming
back by itself after a warning closes), its `NBT_AA_*` options in
`nbt-aad.env` (camera contexts, hard keys, car screens), and its own
settings page.
