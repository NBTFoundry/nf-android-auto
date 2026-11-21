// SPDX-License-Identifier: MPL-2.0
#ifndef NF_AA_DAEMON_BRIDGE_HPP
#define NF_AA_DAEMON_BRIDGE_HPP

#include <stdint.h>

#include "daemon_status.hpp"
#include "nbt/app/car_data.hpp"
#include "nbt/app/lifecycle.hpp"

namespace aa {

class Daemon;

/* What NBT-AAD's iDrive plugin gave the daemon, now from NBT Manager's bridge
 * 1.6 (docs/upstream-interface.md, "Bridge 1.6"):
 *  - the focus hold (`hold`): asked while the daemon's state record says
 *    Android Auto is on screen, `peek` while iDrive's favourites bar is
 *    previewed over it, released when it leaves. When the manager answers
 *    `aside` (iDrive left the blank state), the plugin's rules decide: a key
 *    or the knob that went to the phone, or a call, keeps Android Auto (held
 *    again, or after the call); reverse, park assist and a camera view are
 *    the daemon's own, as is a driving-mode or DSC change (held again);
 *    otherwise, when no such car data came within 1.2 s, the car opened a
 *    screen of its own and the daemon gets "hide";
 *  - car data (`car-data`): gear, park assist, day/night, driving mode, DSC
 *    and iDrive's camera views as the daemon's words, as the plugin sent them;
 *    calls for the rules above. */
class DaemonBridge {
 public:
  enum Field { kGear, kPdc, kDayNight, kMode, kMode35up, kDsc, kContext, kCall0, kCall1, kFields };
  /* The declared names, in Field order (src/protocol.hpp). */
  static const char* car_name(unsigned field);

  DaemonBridge();
  void init(nbt::app::Lifecycle& lc, uint64_t now_ms);
  /* Every loop; `state` is the daemon's record, NULL when unreadable. */
  void poll(nbt::app::Lifecycle& lc, Daemon& daemon, const DaemonState* state, uint64_t now_ms);

  /* For tests: the daemon's word for one car-data value, or "" when nothing
   * is to be sent; keeps the field's state as poll() does. */
  const char* word_for(unsigned field, const char* value, uint64_t now_ms);
  bool call_active() const { return call_[0] || call_[1]; }
  /* Reverse, park assist or a camera view: the daemon steps aside itself. */
  bool car_view() const;
  /* A key, the knob or a driving-mode / DSC change in the last seconds. */
  bool driver_input(const DaemonState& state, uint64_t now_ms) const;
  /* The answer to the manager's `aside`: 'h' hide, 'k' hold again, 'c' wait
   * for the call's end, '-' nothing (the daemon decides). */
  char aside_action(const DaemonState* state, uint64_t now_ms) const;

 private:
  void request(nbt::app::Lifecycle& lc, int word, const char* why);
  void save_start_value(const char* file, const char* word);

  bool hold_;
  int requested_; /* the last request, 0 none */
  uint64_t aside_sequence_;
  uint64_t aside_ms_; /* an `aside` waiting for the car data that explains it, 0 none */
  bool rehold_after_call_;
  bool cars_on_;
  nbt::app::CarData cars_;
  uint64_t sequence_[kFields];
  int last_[kFields];
  bool call_[2];
  uint64_t init_ms_, mode_change_ms_, dsc_change_ms_, next_read_ms_;
  char word_[48];
};

}  // namespace aa

#endif  // NF_AA_DAEMON_BRIDGE_HPP
