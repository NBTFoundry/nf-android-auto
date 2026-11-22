// SPDX-License-Identifier: MPL-2.0
#include "daemon_bridge.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cds_parse.hpp" /* NBT-AAD's own reader (rhmiapp/), as its plugin parsed the values */
#include "daemon.hpp"
#include "nbt/app/log.hpp"
#include "protocol.hpp"

namespace aa {

namespace {

using nbt::app::Lifecycle;
using nbt::app::log_info;

const unsigned kReadEveryMs = 250;
const uint64_t kInputMs = 5000; /* a driving-mode / DSC change counts as the driver's for 5 s */
const uint64_t kLateFirstModeMs = 7000; /* a first mode this long after the start is a change */
/* iDrive's own screen for a driving-mode or DSC change, or a camera, can come
 * before the car data that explains it (dev122: the driving-mode screen, then
 * the mode 0.5 s later): "hide" waits this long for it. */
const uint64_t kAsideSettleMs = 1200;
/* The daemon's and the plugin's defaults: NBT_GEAR_REVERSE, NBT_PDC_ACTIVE,
 * NBT_AA_CAMERA_CTX (iDrive's camera views). */
const int kReverseGear = 2;
const int kPdcActive = 1;
const int kCameraContexts[] = {692, 1023};
const int kNone = -1000;
const int kInvalidMode[2] = {15, 63};
const int kInvalidDsc = 15;
/* The state record's overlay (src/protocol.hpp): BACK held, then a key, a
 * car control and the knob alone that went to the phone in the last 3 s. */
const int kOverlayBackHeld = 2;
const int kOverlayKnob = 5;

const char* const kNames[DaemonBridge::kFields] = {kCdsGear,    kCdsPdc,      kCdsDayNight,
                                                   kCdsMode,    kCdsMode35up, kCdsDsc,
                                                   kCdsContext, kCdsCall0,    kCdsCall1};

/* The first integer in a value (a bare number or one JSON field), else -1. */
int first_int(const char* value) {
  for (const char* p = value; *p; ++p) {
    if (*p >= '0' && *p <= '9') return atoi(p);
  }
  return -1;
}

bool camera_context(int context) {
  for (size_t i = 0; i < sizeof(kCameraContexts) / sizeof(kCameraContexts[0]); ++i) {
    if (kCameraContexts[i] == context) return true;
  }
  return false;
}

const char* hold_name(int word) {
  return word == Lifecycle::kHold ? "hold" : word == Lifecycle::kPeek ? "peek" : "release";
}

bool on_screen_request(int word) {
  return word == Lifecycle::kHold || word == Lifecycle::kPeek;
}

}  // namespace

const char* DaemonBridge::car_name(unsigned field) {
  return field < kFields ? kNames[field] : "";
}

DaemonBridge::DaemonBridge()
    : hold_(false),
      requested_(0),
      aside_sequence_(0),
      aside_ms_(0),
      rehold_after_call_(false),
      cars_on_(false),
      cars_(),
      init_ms_(0),
      mode_change_ms_(0),
      dsc_change_ms_(0),
      next_read_ms_(0) {
  for (unsigned i = 0; i < kFields; ++i) {
    sequence_[i] = 0;
    last_[i] = kNone;
  }
  call_[0] = call_[1] = false;
  word_[0] = '\0';
}

void DaemonBridge::init(Lifecycle& lc, uint64_t now_ms) {
  hold_ = lc.has_cap("hold");
  cars_on_ = lc.has_cap("car-data") && cars_.init();
  init_ms_ = now_ms;
  log_info("bridge: focus hold %s, car data %s, network rules %s", hold_ ? "on" : "off",
           cars_on_ ? "on" : "off", lc.net_rules()[0] ? lc.net_rules() : "none");
}

void DaemonBridge::save_start_value(const char* file, const char* word) {
  char path[256];
  if (snprintf(path, sizeof(path), "%s/%s", shmem_dir(), file) >= static_cast<int>(sizeof(path)))
    return;
  FILE* f = fopen(path, "w");
  if (f) {
    fprintf(f, "%s\n", word);
    fclose(f);
  }
}

const char* DaemonBridge::word_for(unsigned field, const char* value, uint64_t now_ms) {
  word_[0] = '\0';
  if (field >= kFields) return word_;
  int& last = last_[field];
  int v;
  switch (field) {
    case kGear:
    case kPdc:
    case kDayNight: {
      v = field == kDayNight ? nbt::cdsIntField(value, "dayNightChangeOver", -1) : first_int(value);
      if (v == last) break;
      last = v;
      const char* word = field == kGear ? kWordGear : field == kPdc ? kWordPdc : kWordDayNight;
      snprintf(word_, sizeof(word_), "%s%d", word, v);
      save_start_value(field == kGear  ? kGearFile
                       : field == kPdc ? kPdcFile
                                       : kDayNightFile,
                       word_);
      break;
    }
    case kMode:
    case kMode35up: {
      v = nbt::cdsIntField(value, "mode", -1);
      if (v == last) break;
      const int prop = field == kMode ? 0 : 1;
      const int invalid = kInvalidMode[prop];
      const int was = last;
      /* The value before goes along, so the daemon, started after the first
       * value, takes the next as a change; a first value long after the start
       * is the driver changing mode (-2), as the plugin said. */
      int prev = was >= 0 ? was : -1;
      if (was < 0 && v >= 0 && v != invalid && now_ms - init_ms_ >= kLateFirstModeMs) prev = -2;
      if ((was >= 0 && was != invalid && v >= 0 && v != invalid) || prev == -2)
        mode_change_ms_ = now_ms;
      last = v;
      snprintf(word_, sizeof(word_), "%s%d %d %d", kWordDriveMode, prop, v, prev);
      break;
    }
    case kDsc: {
      v = nbt::cdsIntField(value, "DSCActive", -1);
      if (v < 0) v = first_int(value); /* a variant may publish a bare number */
      if (v == last) break;
      const int was = last;
      last = v;
      if (was != kNone) dsc_change_ms_ = now_ms;
      /* A change to or from "not valid" is the signal coming and going, not
       * the driver pressing DSC/DTC. */
      if (was >= 0 && v >= 0 && was != kInvalidDsc && v != kInvalidDsc) {
        snprintf(word_, sizeof(word_), "%s%d %d", kWordDsc, v, was);
      }
      break;
    }
    case kContext: {
      v = nbt::cdsIntField(value, "visibleContext", -1);
      if (v <= 0) break;
      const bool camera = camera_context(v);
      if (camera && last != v) {
        last = v;
        snprintf(word_, sizeof(word_), "%s%d", kWordCamera, v);
      } else if (!camera && last > 0) {
        last = 0;
        snprintf(word_, sizeof(word_), "%s0", kWordCamera);
      }
      break;
    }
    case kCall0:
    case kCall1:
      call_[field - kCall0] = nbt::cdsCallActive(value);
      break;
    default:
      break;
  }
  return word_;
}

bool DaemonBridge::car_view() const {
  return last_[kGear] == kReverseGear || last_[kPdc] == kPdcActive || last_[kContext] > 0;
}

bool DaemonBridge::driver_input(const DaemonState& state, uint64_t now_ms) const {
  if (state.overlay >= kOverlayBackHeld && state.overlay <= kOverlayKnob) return true;
  return (mode_change_ms_ && now_ms - mode_change_ms_ < kInputMs) ||
         (dsc_change_ms_ && now_ms - dsc_change_ms_ < kInputMs);
}

char DaemonBridge::aside_action(const DaemonState* state, uint64_t now_ms) const {
  if (call_active()) return 'c';
  if (!state || state->overlay <= 0 || state->overlay == kOverlayBackHeld || car_view()) return '-';
  return driver_input(*state, now_ms) ? 'k' : 'h';
}

void DaemonBridge::request(Lifecycle& lc, int word, const char* why) {
  if (lc.request_hold(static_cast<Lifecycle::HoldWord>(word))) {
    log_info("bridge: %s (%s)", hold_name(word), why);
  }
  requested_ = word;
}

void DaemonBridge::poll(Lifecycle& lc, Daemon& daemon, const DaemonState* state, uint64_t now_ms) {
  /* Car data first: the hold's rules below look at the gear and the calls. */
  if (cars_on_ && now_ms >= next_read_ms_) {
    next_read_ms_ = now_ms + kReadEveryMs;
    for (unsigned i = 0; i < kFields; ++i) {
      char value[256];
      uint64_t sequence = 0;
      if (!cars_.read(kNames[i], value, sizeof(value), &sequence) || sequence == sequence_[i])
        continue;
      sequence_[i] = sequence;
      const bool was_call = call_active();
      const char* word = word_for(i, value, now_ms);
      if (word[0]) {
        log_info("bridge: %s = %s: \"%s\" to the daemon", kNames[i], value, word);
        daemon.send(word);
      } else if (was_call != call_active()) {
        log_info("bridge: %s = %s: %s", kNames[i], value, call_active() ? "in a call" : "no call");
      }
    }
  }
  if (!hold_) return;
  int want = Lifecycle::kHold;
  if (!state || state->overlay <= 0) {
    want = Lifecycle::kRelease;
  } else if (state->overlay == kOverlayBackHeld && requested_) {
    want = requested_; /* BACK held: the daemon may minimize; leave things as they are */
  } else if (state->preview) {
    want = Lifecycle::kPeek;
  }
  if (want != requested_ && (requested_ || want != Lifecycle::kRelease)) {
    request(lc, want,
            want == Lifecycle::kRelease ? "Android Auto left the screen"
            : want == Lifecycle::kPeek  ? "iDrive's favourites bar over Android Auto"
                                        : "Android Auto on screen");
  }
  if (rehold_after_call_ && !call_active()) {
    rehold_after_call_ = false;
    if (on_screen_request(requested_)) request(lc, Lifecycle::kHold, "the call ended");
  }
  uint64_t sequence = 0;
  if (lc.hold_state(&sequence) == 2 && sequence != aside_sequence_) {
    aside_sequence_ = sequence;
    if (on_screen_request(requested_)) aside_ms_ = now_ms ? now_ms : 1;
  }
  if (!aside_ms_) return;
  if (!on_screen_request(requested_)) { /* Android Auto left by itself meanwhile */
    aside_ms_ = 0;
    return;
  }
  const char action = aside_action(state, now_ms);
  if (action == 'h' && now_ms - aside_ms_ < kAsideSettleMs) return;
  aside_ms_ = 0;
  switch (action) {
    case 'h':
      log_info("bridge: iDrive opened a screen of its own: Android Auto steps aside");
      daemon.send(kHide);
      break;
    case 'k':
      request(lc, Lifecycle::kHold,
              "iDrive left the blank state on a key that went to Android Auto");
      break;
    case 'c':
      log_info(
          "bridge: iDrive's call screen during a call: Android Auto stays, held again after the "
          "call");
      rehold_after_call_ = true;
      break;
    default:
      log_info(
          "bridge: iDrive left the blank state (reverse, park assist, camera or BACK): the daemon "
          "decides");
      break;
  }
}

}  // namespace aa
