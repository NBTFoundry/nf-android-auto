// SPDX-License-Identifier: MPL-2.0
#include "daemon.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "daemon_status.hpp"
#include "nbt/app/child.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"
#include "nbt/app/parse.hpp"
#include "protocol.hpp"

extern "C" char** environ;

namespace aa {

namespace {

using nbt::app::log_info;

const size_t kEnvMax = 256;
const unsigned kQuitGraceMs = 3000; /* then SIGTERM: iDrive's Force Stop timing */
const unsigned kTermGraceMs = 7000; /* then SIGKILL, 10 s after "quit" */

unsigned control_port() {
  int port = 0;
  const char* value = getenv(kControlPortVar);
  if (value && nbt::app::parse_int_range(value, 1, 65535, &port))
    return static_cast<unsigned>(port);
  return kControlPort;
}

/* Whether process `pid` runs the program at `path`. */
bool runs_program(pid_t pid, const char* path) {
  char proc[64];
#ifdef __QNXNTO__
  snprintf(proc, sizeof(proc), "/proc/%d/exefile", static_cast<int>(pid));
  FILE* f = fopen(proc, "r");
  if (!f) return false;
  char exe[512] = "";
  const bool read = fgets(exe, sizeof(exe), f) != NULL;
  fclose(f);
  exe[strcspn(exe, "\n")] = '\0';
  return read && strcmp(exe, path) == 0;
#else
  /* Host tests run a script: its path is an argument of the interpreter. */
  snprintf(proc, sizeof(proc), "/proc/%d/cmdline", static_cast<int>(pid));
  const int fd = open(proc, O_RDONLY);
  if (fd < 0) return false;
  char args[1024];
  const ssize_t n = read(fd, args, sizeof(args) - 1);
  close(fd);
  if (n <= 0) return false;
  args[n] = '\0';
  for (ssize_t i = 0; i < n; i += static_cast<ssize_t>(strlen(args + i)) + 1) {
    if (strcmp(args + i, path) == 0) return true;
  }
  return false;
#endif
}

}  // namespace

Daemon::Daemon() : pid_(-1), sock_(-1) {
  path_[0] = '\0';
}

Daemon::~Daemon() {
  if (sock_ >= 0) close(sock_);
}

bool Daemon::install(const char* dir) {
  const char* why = NULL;
  const int rc = nbt::app::extract_own_payload(dir, kDaemonName, path_, sizeof(path_), &why);
  if (rc == 1) return true;
  log_info("daemon: %s", rc == 0 ? "none appended to this executable" : why ? why : "?");
  path_[0] = '\0';
  return false;
}

bool Daemon::loads() {
  char* env[kEnvMax];
  if (nbt::app::child_environment(environ, NULL, env, kEnvMax, true) < 0) return false;
  char arg[sizeof(kLicenseArg)];
  memcpy(arg, kLicenseArg, sizeof(arg));
  char* argv[] = {path_, arg, NULL};
  const int null_fd = open("/dev/null", O_RDWR);
  const int fds[3] = {null_fd, null_fd, STDERR_FILENO};
  pid_t pid = -1;
  const int err = nbt::app::spawn_child(path_, argv, env, fds, 3, false, &pid);
  if (null_fd >= 0) close(null_fd);
  if (err != 0) {
    log_info("daemon: cannot run %s (errno %d)", path_, err);
    return false;
  }
  const int status = nbt::app::stop_child(pid, false, 10000, 1000);
  if (status == 0) return true;
  log_info("daemon: %s %s ended with status 0x%x", path_, kLicenseArg,
           static_cast<unsigned>(status));
  return false;
}

bool Daemon::open_socket() {
  if (sock_ >= 0) return true;
  sock_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_ < 0) return false;
  (void)fcntl(sock_, F_SETFD, FD_CLOEXEC);
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(static_cast<unsigned short>(control_port()));
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(sock_, reinterpret_cast<struct sockaddr*>(&to), sizeof(to)) != 0) {
    close(sock_);
    sock_ = -1;
    return false;
  }
  return true;
}

bool Daemon::send(const char* word) {
  if (!open_socket()) return false;
  const size_t len = strlen(word);
  return ::send(sock_, word, len, 0) == static_cast<ssize_t>(len);
}

bool Daemon::ping(unsigned timeout_ms) {
  if (!open_socket()) return false;
  char reply[16];
  while (recv(sock_, reply, sizeof(reply), MSG_DONTWAIT) >= 0) {
  } /* answers that came late */
  if (!send(kPing)) return false;
  const uint64_t deadline = nbt::app::monotonic_ms() + timeout_ms;
  for (;;) {
    const uint64_t now = nbt::app::monotonic_ms();
    if (now >= deadline) return false;
    struct pollfd pfd = {sock_, POLLIN, 0};
    const int ready = poll(&pfd, 1, static_cast<int>(deadline - now));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) return false;
    const ssize_t n = recv(sock_, reply, sizeof(reply) - 1, 0);
    if (n < 0) return false; /* nothing listens: ECONNREFUSED */
    reply[n] = '\0';
    if (strncmp(reply, kPong, sizeof(kPong) - 1) == 0) return true;
  }
}

bool Daemon::answers(unsigned timeout_ms) {
  return ping(timeout_ms);
}

bool Daemon::answering_is_ours() {
  DaemonState state;
  return path_[0] && read_state(&state) && runs_program(state.pid, path_);
}

bool Daemon::quit_answering(unsigned timeout_ms) {
  send(kQuit);
  const uint64_t deadline = nbt::app::monotonic_ms() + timeout_ms;
  while (nbt::app::monotonic_ms() < deadline) {
    if (!ping(200)) return true;
    usleep(200000);
  }
  return false;
}

bool Daemon::start(char* const* first) {
  char* env[kEnvMax];
  if (nbt::app::child_environment(environ, first, env, kEnvMax, true) < 0) {
    log_info("daemon: environment too large");
    return false;
  }
  char* argv[] = {path_, NULL};
  const int null_fd = open("/dev/null", O_RDONLY);
  const int fds[3] = {null_fd, STDOUT_FILENO, STDERR_FILENO};
  /* In this process's group: whatever stops the adapter's group stops it too. */
  const int err = nbt::app::spawn_child(path_, argv, env, fds, 3, false, &pid_);
  if (null_fd >= 0) close(null_fd);
  if (err != 0) {
    log_info("daemon: cannot start %s (errno %d)", path_, err);
    pid_ = -1;
    return false;
  }
  log_info("daemon: started %s, pid %d", path_, static_cast<int>(pid_));
  return true;
}

bool Daemon::running(int* status) {
  if (pid_ <= 0) return false;
  if (!nbt::app::child_exited(pid_, status)) return true;
  pid_ = -1;
  return false;
}

int Daemon::stop() {
  if (pid_ <= 0) return -1;
  send(kQuit);
  const int status = nbt::app::stop_child(pid_, false, kQuitGraceMs, kTermGraceMs);
  log_info("daemon: pid %d stopped, status 0x%x", static_cast<int>(pid_),
           static_cast<unsigned>(status));
  pid_ = -1;
  return status;
}

}  // namespace aa
