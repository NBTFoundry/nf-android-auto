// SPDX-License-Identifier: MPL-2.0
// Unit tests: the settings table as the daemon receives it, the status and
// state records, the daemon log's copy.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daemon_log.hpp"
#include "daemon_settings.hpp"
#include "daemon_status.hpp"
#include "nbt/app/settings.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                   \
    }                                                                 \
  } while (0)

bool has_env(const aa::DaemonSettings& s, const char* entry) {
  for (unsigned i = 0; i < s.env_count; ++i) {
    if (strcmp(s.env[i], entry) == 0) return true;
  }
  return false;
}

bool has_var(const aa::DaemonSettings& s, const char* name) {
  const size_t n = strlen(name);
  for (unsigned i = 0; i < s.env_count; ++i) {
    if (strncmp(s.env[i], name, n) == 0 && s.env[i][n] == '=') return true;
  }
  return false;
}

int flag(const aa::DaemonSettings& s, const char* name) {
  for (unsigned i = 0; i < s.flag_count; ++i) {
    if (strcmp(s.flags[i].name, name) == 0) return s.flags[i].present ? 1 : 0;
  }
  return -1;
}

void test_defaults() {
  nbt::app::Settings none;
  aa::DaemonSettings s;
  CHECK(aa::daemon_settings(none, &s));
  CHECK(has_env(s, "NBT_AUDIO_MODE=bt"));
  CHECK(has_env(s, "NBT_ALT_WIFI=DIRECT"));
  CHECK(has_env(s, "NBT_USB=0"));
  CHECK(!has_var(s, "NBT_VEHICLE")); /* auto: the daemon reads carplay.ini */
  CHECK(flag(s, "show-drivemode") == 0);
  CHECK(flag(s, "favourite-bar.disabled") == 0); /* bar on */
  CHECK(s.env[s.env_count] == NULL);
}

void test_choices() {
  nbt::app::Settings chosen;
  CHECK(chosen.set("audio", "native"));
  CHECK(chosen.set("wifi", "carplay"));
  CHECK(chosen.set("vehicle", "toyota"));
  CHECK(chosen.set("turn_timing", "time"));
  CHECK(chosen.set("drive_mode", "on"));
  CHECK(chosen.set("favourites", "off"));
  CHECK(chosen.set("usb", "bogus")); /* not a choice: the default */
  aa::DaemonSettings s;
  CHECK(aa::daemon_settings(chosen, &s));
  CHECK(has_env(s, "NBT_AUDIO_MODE=browser"));
  CHECK(has_env(s, "NBT_ALT_WIFI=CARPLAY"));
  CHECK(has_env(s, "NBT_VEHICLE=toyota"));
  CHECK(has_env(s, "NBT_KOMBI_NAV_APPROACH=time"));
  CHECK(has_env(s, "NBT_USB=0"));
  CHECK(flag(s, "show-drivemode") == 1);
  CHECK(flag(s, "favourite-bar.disabled") == 1);
}

/* Every choice of every row gives the daemon something. */
void test_every_choice() {
  const aa::SettingRow* rows = NULL;
  const unsigned count = aa::setting_rows(&rows);
  CHECK(count >= 1 && count <= 12);
  for (unsigned r = 0; r < count; ++r) {
    char choices[128];
    snprintf(choices, sizeof(choices), "%s", rows[r].choices);
    for (char* choice = strtok(choices, ","); choice; choice = strtok(NULL, ",")) {
      nbt::app::Settings chosen;
      CHECK(chosen.set(rows[r].key, choice));
      aa::DaemonSettings s;
      CHECK(aa::daemon_settings(chosen, &s));
      CHECK(rows[r].flag ? flag(s, rows[r].target) >= 0 : true);
    }
  }
}

void test_flags_on_disk() {
  char dir[] = "/tmp/nf-aa-flags-XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  nbt::app::Settings chosen;
  CHECK(chosen.set("drive_mode", "on"));
  aa::DaemonSettings s;
  CHECK(aa::daemon_settings(chosen, &s));
  CHECK(aa::apply_flags(s, dir));
  char path[128];
  snprintf(path, sizeof(path), "%s/show-drivemode", dir);
  CHECK(access(path, F_OK) == 0);
  CHECK(chosen.set("drive_mode", "off"));
  CHECK(aa::daemon_settings(chosen, &s));
  CHECK(aa::apply_flags(s, dir));
  CHECK(access(path, F_OK) != 0);
  CHECK(rmdir(dir) == 0);
}

void test_status() {
  CHECK(strcmp(aa::status_label("waiting-phone|1|0|1|-"), "Waiting for phone") == 0);
  CHECK(strcmp(aa::status_label("waiting-phone|0|1|0|-"), "Waiting for phone") == 0);
  CHECK(strcmp(aa::status_label("waiting-phone|0|0|1|-"), "No Wi-Fi: USB only") == 0);
  CHECK(strcmp(aa::status_label("waiting-phone|0|0|0|-"), "No Wi-Fi or USB") == 0);
  CHECK(strcmp(aa::status_label("connecting|1|0|1|-"), "Connecting") == 0);
  CHECK(strcmp(aa::status_label("connected|1|0|1|wifi"), "Connected (Wi-Fi)") == 0);
  CHECK(strcmp(aa::status_label("connected|1|0|1|usb"), "Connected (USB)") == 0);
  CHECK(strcmp(aa::status_label("connected|1|0|1|-"), "Connected") == 0);
  CHECK(aa::status_label("dreaming|1|0|1|-") == NULL); /* a newer daemon's state */
  CHECK(aa::status_label("connected|1|0") == NULL);    /* torn record */
  CHECK(aa::status_label("") == NULL);
  const char* const longest[] = {"waiting-phone|0|0|1|-", "connected|1|0|1|wifi"};
  for (size_t i = 0; i < sizeof(longest) / sizeof(longest[0]); ++i) {
    CHECK(strlen(aa::status_label(longest[i])) <= 24);
  }

  aa::DaemonState st;
  CHECK(aa::parse_state("connected        4242 1 0 0\n", &st));
  CHECK(strcmp(st.state, "connected") == 0 && st.pid == 4242 && st.overlay == 1);
  CHECK(!aa::parse_state("idle 0 0", &st));
  CHECK(!aa::parse_state("idle", &st));
}

/* copy_log_ends of `text` (as a file), into a string. */
bool copied(const char* text, size_t head, size_t tail, char* out, size_t size) {
  char path[] = "/tmp/nf-aa-log-XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) return false;
  const bool wrote = write(fd, text, strlen(text)) == static_cast<ssize_t>(strlen(text));
  close(fd);
  FILE* mem = fmemopen(out, size, "w");
  const bool ok = wrote && mem && aa::copy_log_ends(path, mem, head, tail);
  if (mem) fclose(mem);
  unlink(path);
  return ok;
}

void test_log_copy() {
  char out[512];
  CHECK(copied("one\ntwo\n", 64, 64, out, sizeof(out)));
  CHECK(strstr(out, "(8 bytes) ----\none\ntwo\n---- end of ") != NULL);
  /* Cut on line ends: the head keeps whole lines, the tail starts on one. */
  CHECK(copied("aaaa\nbbbb\ncccc\ndddd\neeee\n", 7, 8, out, sizeof(out)));
  CHECK(strstr(out, "(25 bytes): the first 7 and the last 8 ----\naaaa\n---- ... ----\neeee\n---- end of ") != NULL);
  CHECK(strstr(out, "bbbb") == NULL && strstr(out, "dddd") == NULL);
  /* No newline at the end: the end marker still starts a line. */
  CHECK(copied("last words", 64, 64, out, sizeof(out)));
  CHECK(strstr(out, "last words\n---- end of ") != NULL);
  FILE* devnull = fopen("/dev/null", "w");
  CHECK(!aa::copy_log_ends("/nonexistent/nbt-aad.log", devnull));
  if (devnull) fclose(devnull);
}

}  // namespace

int main() {
  test_defaults();
  test_choices();
  test_every_choice();
  test_flags_on_disk();
  test_status();
  test_log_copy();
  if (g_failures) {
    fprintf(stderr, "%d adapter checks failed\n", g_failures);
    return 1;
  }
  printf("adapter unit tests passed\n");
  return 0;
}
