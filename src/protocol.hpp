// SPDX-License-Identifier: MPL-2.0
#ifndef NF_AA_PROTOCOL_HPP
#define NF_AA_PROTOCOL_HPP

// What this adapter knows of NBT-AAD's daemon (third_party/nbt-aad): its
// control port and words, its status record and its command line. Each value
// is written here once; tools/check_protocol.py finds every one in the
// upstream file its "upstream:" note names, so a pin that changes one fails
// the build (docs/upstream-interface.md).

namespace aa {

// The program appended to the adapter, copied into the data folder.
const char kDaemonName[] = "nbt-aad";  // upstream: milestone-7/src/nbt_paths.hpp

// Prints the licence and exits: proves the program loads on this head unit.
const char kLicenseArg[] = "--license";  // upstream: milestone-7/src/main.cpp

// UDP on 127.0.0.1. The first daemon owns the port; a second one sends
// "show" to it and exits, which is how a running daemon is found.
const unsigned kControlPort = 52790;  // upstream: milestone-7/src/main.cpp
// Another port, for host tests only (the manager passes no such variable).
const char kControlPortVar[] = "NBT_CTL_PORT";  // upstream: milestone-7/src/main.cpp
const char kPing[] = "ping";                    // upstream: milestone-7/src/main.cpp
const char kPong[] = "pong";                    // upstream: milestone-7/src/main.cpp
const char kShow[] = "show";                    // upstream: milestone-7/src/main.cpp
const char kQuit[] = "quit";                    // upstream: milestone-7/src/main.cpp

// The state record, "<state> <pid> <overlay> ...", rewritten in place in the
// shared-memory folder: which daemon runs, and whether it is on screen.
const char kStateFile[] = "nbt-aad-state";  // upstream: milestone-7/src/main.cpp

// The status record, "<state>|<carplay>|<wfd>|<usb>|<transport>", the same way.
const char kStatusFile[] = "nbt-aad-status";   // upstream: milestone-7/src/main.cpp
const char kStateWaiting[] = "waiting-phone";  // upstream: milestone-7/src/main.cpp
const char kStateConnecting[] = "connecting";  // upstream: milestone-7/src/main.cpp
const char kStateConnected[] = "connected";    // upstream: milestone-7/src/main.cpp
const char kTransportWifi[] = "wifi";          // upstream: milestone-7/src/main.cpp
const char kTransportUsb[] = "usb";            // upstream: milestone-7/src/main.cpp

// Its own log in the same folder, fresh at each start (unless it logs to a
// USB stick): copied into the application log when it ends.
const char kLogFile[] = "nbt-aad.log";  // upstream: milestone-7/src/usb_log.hpp

// The daemon's own settings environment: its settings folder (seam S1).
const char kDataDirVar[] = "NBT_DATA_DIR";  // upstream: milestone-7/src/nbt_paths.hpp

// Left to the packet filter's own rules (NBT Manager opens the daemon's port,
// capability `net`); without it the daemon switches the filter off.
const char kKeepPfVar[] = "NBT_KEEP_PF";  // upstream: milestone-7/src/main.cpp

// What NBT-AAD's iDrive plugin told the daemon, which the adapter now does
// from NBT Manager's bridge 1.6 (src/daemon_bridge.cpp). Words on the control
// port, each followed by its numbers:
const char kHide[] = "hide";                 // upstream: milestone-7/src/main.cpp
const char kWordGear[] = "gear ";            // upstream: milestone-7/src/main.cpp
const char kWordPdc[] = "pdc ";              // upstream: milestone-7/src/main.cpp
const char kWordDayNight[] = "daynight ";    // upstream: milestone-7/src/main.cpp
const char kWordDriveMode[] = "drivemode ";  // upstream: milestone-7/src/main.cpp
const char kWordDsc[] = "dsc ";              // upstream: milestone-7/src/main.cpp
const char kWordCamera[] = "camera ";        // upstream: milestone-7/src/main.cpp
// The last gear, park-assist and day/night words, read by a daemon at start.
const char kGearFile[] = "nbt-aad-gear";          // upstream: milestone-7/src/main.cpp
const char kPdcFile[] = "nbt-aad-pdc";            // upstream: milestone-7/src/main.cpp
const char kDayNightFile[] = "nbt-aad-daynight";  // upstream: milestone-7/src/main.cpp
// The iDrive car data the plugin read (CDS names); release/package.base signs
// the same list as the package's `bridge-cds`.
// clang-format off
const char kCdsGear[] = "driving.gear";                    // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsPdc[] = "sensors.PDCStatus";                // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsDayNight[] = "vehicle.dayNightChangeOver";  // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsMode[] = "driving.mode";                    // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsMode35up[] = "driving.mode35up";            // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsDsc[] = "driving.DSCActive";                // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsContext[] = "hmi.graphicalContext";         // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsCall0[] = "communication.callStatus0";      // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
const char kCdsCall1[] = "communication.callStatus1";      // upstream: milestone-7/rhmiapp/nbt_aad_app.cpp
// clang-format on

}  // namespace aa

#endif  // NF_AA_PROTOCOL_HPP
