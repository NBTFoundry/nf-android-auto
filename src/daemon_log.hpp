// SPDX-License-Identifier: MPL-2.0
#ifndef NF_AA_DAEMON_LOG_HPP
#define NF_AA_DAEMON_LOG_HPP

#include <stddef.h>
#include <stdio.h>

namespace aa {

/* The daemon keeps its own log in the shared-memory folder (src/protocol.hpp
 * kLogFile), fresh at each start and kept after it ends. When it ends, its
 * beginning (the Wi-Fi source, Bluetooth and settings it started with) and its
 * end (what it was doing last) are copied to `out`, the manager's application
 * log, which the phone app shares. */
const size_t kLogHeadBytes = 8 * 1024;
const size_t kLogTailBytes = 16 * 1024;

/* Copies the head and tail of the file at `path` to `out`, between two marker
 * lines; the cut falls on line ends. Returns false when there is no log. */
bool copy_log_ends(const char* path, FILE* out, size_t head = kLogHeadBytes, size_t tail = kLogTailBytes);

/* copy_log_ends of the daemon's log, to standard output. */
void copy_daemon_log();

}  // namespace aa

#endif  // NF_AA_DAEMON_LOG_HPP
