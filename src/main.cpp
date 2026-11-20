// SPDX-License-Identifier: MPL-2.0
// nbt-android-auto: NBT-AAD's daemon as an NBT Manager application.
//
// The manager starts this adapter; the adapter copies the daemon it carries
// into the application's data folder, starts it with the settings the manager
// keeps, and stands for it on the bridge: readiness and heartbeats while the
// daemon answers its control port, "show" for the manager's bring-to-front,
// the daemon's status as the menu label, and a stop the way iDrive's Force
// Stop did. The daemon itself is NBT-AAD, unmodified (docs/upstream-interface.md).
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon.hpp"
#include "daemon_log.hpp"
#include "daemon_settings.hpp"
#include "daemon_status.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/lifecycle.hpp"
#include "nbt/app/log.hpp"
#include "protocol.hpp"

using nbt::app::Lifecycle;
using nbt::app::log_info;
using nbt::app::monotonic_ms;

namespace {

const unsigned kStartAnswerMs = 4500; /* the manager wants readiness within 5 s */
const unsigned kPingEveryMs = 2000;
const unsigned kPingTimeoutMs = 500;
const int kPingMissesMax = 3; /* then the daemon is taken as hung */
const unsigned kLoopMs = 100;

/* The manager's health check: the daemon loads here, nothing else is touched
 * (no Bluetooth, Wi-Fi, firewall or screen). */
int run_check(Lifecycle& lc, aa::Daemon& daemon) {
  if (!daemon.loads()) return 1;
  log_info("check: %s loads on this head unit", daemon.path());
  if (!lc.send_ready()) return 1;
  while (!lc.stopping()) {
    lc.tick(monotonic_ms(), "Checked", NULL);
    usleep(kLoopMs * 1000);
  }
  return 0;
}

/* A daemon already on the port: one this adapter left behind is stopped; any
 * other (a hand or NBTApps install) is left alone and this start fails. */
bool claim_port(Lifecycle& lc, aa::Daemon& daemon) {
  if (!daemon.answers(300)) return true;
  if (daemon.answering_is_ours()) {
    log_info("a daemon from an earlier start still runs: stopping it");
    return daemon.quit_answering(10000);
  }
  log_info("another NBT-AAD daemon answers on the control port: stop that install first");
  lc.send_status(monotonic_ms(), "Another install runs");
  return false;
}

/* Waits for the new daemon's first answer. */
bool await_answer(Lifecycle& lc, aa::Daemon& daemon) {
  const uint64_t deadline = monotonic_ms() + kStartAnswerMs;
  while (monotonic_ms() < deadline && !lc.stopping()) {
    int status = 0;
    if (!daemon.running(&status)) {
      log_info("daemon ended while starting, status 0x%x", static_cast<unsigned>(status));
      return false;
    }
    if (daemon.ping(kPingTimeoutMs)) return true;
  }
  log_info("daemon did not answer within %u ms", kStartAnswerMs);
  return false;
}

/* The started daemon, until it ends or the manager stops the application. */
int watch(Lifecycle& lc, aa::Daemon& daemon) {
  if (!await_answer(lc, daemon) || !lc.send_ready()) {
    daemon.stop();
    return 1;
  }
  int misses = 0;
  uint64_t last_ping = monotonic_ms();
  while (!lc.stopping()) {
    int status = 0;
    if (!daemon.running(&status)) {
      log_info("daemon ended, status 0x%x", static_cast<unsigned>(status));
      return 1;
    }
    if (lc.consume_show_requested()) daemon.send(aa::kShow);
    const uint64_t now = monotonic_ms();
    if (now - last_ping >= kPingEveryMs) {
      last_ping = now;
      misses = daemon.ping(kPingTimeoutMs) ? 0 : misses + 1;
      if (misses >= kPingMissesMax) {
        log_info("daemon stopped answering");
        daemon.stop();
        return 1;
      }
    }
    if (misses == 0) lc.tick(now, aa::current_status_label("Running"), NULL);
    usleep(kLoopMs * 1000);
  }
  daemon.stop();
  return 0;
}

int run_user(Lifecycle& lc, aa::Daemon& daemon, const char* data_dir) {
  aa::DaemonSettings settings;
  if (!aa::daemon_settings(lc.settings(), &settings)) {
    log_info("settings table does not apply");
    return 1;
  }
  if (!aa::apply_flags(settings, data_dir)) return 1;
  if (!claim_port(lc, daemon) || !daemon.start(settings.env)) return 1;
  const int rc = watch(lc, daemon);
  aa::copy_daemon_log(); /* what it did, for the log the phone app shares */
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  nbt::app::log_init("android-auto");
  Lifecycle lc;
  if (const int rc = lc.init(argc, argv)) return rc;
  nbt::app::log_install_crash_handlers();
  const char* data_dir = lc.data_dir();
  if (!data_dir[0]) {
    log_info("no data folder (NBT_DATA_DIR)");
    return 1;
  }
  aa::Daemon daemon;
  if (!daemon.install(data_dir)) return 1;
  const int rc = lc.is_check_mode() ? run_check(lc, daemon) : run_user(lc, daemon, data_dir);
  lc.close_fds();
  return rc;
}
