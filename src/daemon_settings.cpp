// SPDX-License-Identifier: MPL-2.0
#include "daemon_settings.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nbt/app/log.hpp"

namespace aa {

namespace {

#define AA_ENV false
#define AA_FLAG true
#define AA_SETTING(key, label, fallback, choices, kind, target, values) \
  {#key, label, fallback, choices, kind, target, values},
const SettingRow kRows[] = {
#include "settings.def"
};
#undef AA_SETTING
#undef AA_FLAG
#undef AA_ENV

const unsigned kRowCount = sizeof(kRows) / sizeof(kRows[0]);

/* The position of `word` in the comma list `list`, or -1. */
int position(const char* list, const char* word) {
  const size_t len = strlen(word);
  int index = 0;
  for (const char* p = list;; ++index) {
    const char* end = strchr(p, ',');
    const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
    if (n == len && strncmp(p, word, n) == 0) return index;
    if (!end) return -1;
    p = end + 1;
  }
}

/* The item at `index` of the comma list `list`, copied to `out`. */
bool item(const char* list, int index, char* out, size_t size) {
  const char* p = list;
  for (int i = 0; i < index; ++i) {
    p = strchr(p, ',');
    if (!p) return false;
    ++p;
  }
  const char* end = strchr(p, ',');
  const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
  if (n == 0 || n >= size) return false;
  memcpy(out, p, n);
  out[n] = '\0';
  return true;
}

}  // namespace

unsigned setting_rows(const SettingRow** rows) {
  *rows = kRows;
  return kRowCount;
}

bool daemon_settings(const nbt::app::Settings& chosen, DaemonSettings* out) {
  memset(out, 0, sizeof(*out));
  for (unsigned r = 0; r < kRowCount; ++r) {
    const SettingRow& row = kRows[r];
    const char* value = chosen.get(row.key);
    int index = value ? position(row.choices, value) : -1;
    if (index < 0) index = position(row.choices, row.fallback);
    char given[DaemonSettings::kEntryMax];
    if (index < 0 || !item(row.values, index, given, sizeof(given))) return false;
    if (row.flag) {
      if (out->flag_count >= DaemonSettings::kFlagsMax || strchr(row.target, '/')) return false;
      out->flags[out->flag_count].name = row.target;
      out->flags[out->flag_count].present = strcmp(given, "1") == 0;
      ++out->flag_count;
    } else if (strcmp(given, "-") != 0) {
      if (out->env_count >= DaemonSettings::kEnvMax) return false;
      char* entry = out->env_text[out->env_count];
      const int n = snprintf(entry, DaemonSettings::kEntryMax, "%s=%s", row.target, given);
      if (n < 0 || n >= DaemonSettings::kEntryMax) return false;
      out->env[out->env_count++] = entry;
    }
  }
  out->env[out->env_count] = NULL;
  return true;
}

bool apply_flags(const DaemonSettings& settings, const char* dir) {
  bool ok = true;
  for (unsigned i = 0; i < settings.flag_count; ++i) {
    char path[512];
    const DaemonSettings::Flag& flag = settings.flags[i];
    if (snprintf(path, sizeof(path), "%s/%s", dir, flag.name) >= static_cast<int>(sizeof(path))) {
      ok = false;
      continue;
    }
    struct stat st;
    const bool exists = lstat(path, &st) == 0;
    if (exists && !S_ISREG(st.st_mode)) { /* a link or a folder: not a flag this adapter made */
      nbt::app::log_info("flag %s: not a regular file", flag.name);
      ok = false;
    } else if (flag.present) {
      if (!exists) { /* QNX 6.5 has no O_NOFOLLOW: a new flag is created exclusively */
        const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
          close(fd);
        } else {
          nbt::app::log_info("flag %s: cannot create (errno %d)", flag.name, errno);
          ok = false;
        }
      }
    } else if (unlink(path) != 0 && errno != ENOENT) {
      nbt::app::log_info("flag %s: cannot remove (errno %d)", flag.name, errno);
      ok = false;
    }
  }
  return ok;
}

}  // namespace aa
