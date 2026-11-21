// SPDX-License-Identifier: MPL-2.0
#ifndef NF_AA_DAEMON_SETTINGS_HPP
#define NF_AA_DAEMON_SETTINGS_HPP

#include "nbt/app/settings.hpp"

namespace aa {

/* What the manager's settings give the daemon (src/settings.def): variables
 * for its environment and flag files in its data folder. */
struct DaemonSettings {
  enum { kEnvMax = 16, kEntryMax = 64, kFlagsMax = 8 };
  char env_text[kEnvMax][kEntryMax];
  char* env[kEnvMax + 1]; /* NAME=value entries, NULL-terminated */
  unsigned env_count;
  struct Flag {
    const char* name;
    bool present;
  } flags[kFlagsMax];
  unsigned flag_count;
};

/* The table's rows, for tools and tests. */
struct SettingRow {
  const char* key;
  const char* label;
  const char* fallback; /* the default choice */
  const char* choices;
  bool flag; /* AA_FLAG, else AA_ENV */
  const char* target;
  const char* values;
};
unsigned setting_rows(const SettingRow** rows);

/* Each row's value for the chosen choice; the row's default when the setting
 * is missing or not one of its choices. False only for a table that does not
 * fit or does not parse (tests/adapter_test.cpp checks the real one). */
bool daemon_settings(const nbt::app::Settings& chosen, DaemonSettings* out);

/* One more NAME=value entry; false when it does not fit. */
bool add_env(DaemonSettings* settings, const char* name, const char* value);

/* An audio connection id the manager gives (NBT_AUDIO_CONN): 1 to 0xffff, in
 * hexadecimal with 0x, as the daemon's NBT_BROWSER_CONN reads it. */
bool audio_conn_valid(const char* value);

/* Creates the flags that are present and removes the others, in `dir`. False
 * when one cannot be made or removed. */
bool apply_flags(const DaemonSettings& settings, const char* dir);

}  // namespace aa

#endif  // NF_AA_DAEMON_SETTINGS_HPP
