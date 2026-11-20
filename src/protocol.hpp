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

// The daemon's own settings environment: its settings folder (seam S1).
const char kDataDirVar[] = "NBT_DATA_DIR";  // upstream: milestone-7/src/nbt_paths.hpp

}  // namespace aa

#endif  // NF_AA_PROTOCOL_HPP
