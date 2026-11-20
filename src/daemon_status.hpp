// SPDX-License-Identifier: MPL-2.0
#ifndef NF_AA_DAEMON_STATUS_HPP
#define NF_AA_DAEMON_STATUS_HPP

#include <stddef.h>
#include <sys/types.h>

namespace aa {

/* The daemon's shared-memory folder: /dev/shmem on the head unit; on a host,
 * NF_AA_SHMEM_DIR (tests) or /dev/shm. */
const char* shmem_dir();

/* The state record: which daemon writes it and whether it is on screen. */
struct DaemonState {
  char state[24];
  pid_t pid;
  int overlay; /* 0 hidden, 1-5 on screen (src/protocol.hpp) */
};
bool parse_state(const char* text, DaemonState* out);
bool read_state(DaemonState* out);

/* The menu's status label for a status record (at most 24 characters):
 * "Waiting for phone", "No Wi-Fi: USB only", "Connecting",
 * "Connected (Wi-Fi)", "Connected (USB)". NULL for a record it cannot read. */
const char* status_label(const char* record);
/* The label for the current status file, or `fallback`. */
const char* current_status_label(const char* fallback);

}  // namespace aa

#endif  // NF_AA_DAEMON_STATUS_HPP
