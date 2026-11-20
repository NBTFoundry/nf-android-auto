// SPDX-License-Identifier: MPL-2.0
#ifndef NF_AA_DAEMON_HPP
#define NF_AA_DAEMON_HPP

#include <stddef.h>
#include <sys/types.h>

namespace aa {

/* NBT-AAD's daemon, carried in this executable (nbt/app/payload.h): copied
 * into the data folder, started, watched and stopped, and spoken to on its
 * control port (src/protocol.hpp). */
class Daemon {
 public:
  Daemon();
  ~Daemon();

  /* Copies the daemon into `dir`. */
  bool install(const char* dir);
  const char* path() const { return path_; }

  /* Runs `<daemon> --license` to its end: the program and its libraries
   * load on this head unit. Touches nothing else. */
  bool loads();

  /* Whether a daemon answers on the control port, and whether it is this
   * one's program (left by an adapter that did not stop it). */
  bool answers(unsigned timeout_ms);
  bool answering_is_ours();

  /* Starts it with `first` ahead of this process's environment (iDrive's
   * preloads included), its output on this process's. */
  bool start(char* const* first);
  pid_t pid() const { return pid_; }
  /* Whether it still runs; when it has ended, *status gets its wait status. */
  bool running(int* status);

  /* One word on the control port; ping waits for the answer. */
  bool send(const char* word);
  bool ping(unsigned timeout_ms);

  /* "quit", then SIGTERM after 3 s and SIGKILL after 10 s, as iDrive's
   * Force Stop did. Returns the wait status, or -1. */
  int stop();
  /* Asks whatever daemon answers to quit, and waits up to `timeout_ms` for
   * the port to go quiet. */
  bool quit_answering(unsigned timeout_ms);

 private:
  Daemon(const Daemon&);
  Daemon& operator=(const Daemon&);
  bool open_socket();

  char path_[256];
  pid_t pid_;
  int sock_;
};

}  // namespace aa

#endif  // NF_AA_DAEMON_HPP
