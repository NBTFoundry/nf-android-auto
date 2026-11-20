// SPDX-License-Identifier: MPL-2.0
#include "daemon_status.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "protocol.hpp"

namespace aa {

namespace {

/* A small record written in place: read once, bounded. */
bool read_record(const char* name, char* out, size_t size) {
  char path[256];
  if (snprintf(path, sizeof(path), "%s/%s", shmem_dir(), name) >= static_cast<int>(sizeof(path))) {
    return false;
  }
  const int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  struct stat st;
  const ssize_t n = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) ? read(fd, out, size - 1) : -1;
  close(fd);
  if (n <= 0) return false;
  out[n] = '\0';
  out[strcspn(out, "\r\n")] = '\0';
  return true;
}

/* Field `index` of a '|'-separated record, copied to `out`. */
bool field(const char* record, int index, char* out, size_t size) {
  const char* p = record;
  for (int i = 0; i < index; ++i) {
    p = strchr(p, '|');
    if (!p) return false;
    ++p;
  }
  const size_t n = strcspn(p, "|");
  if (n >= size) return false;
  memcpy(out, p, n);
  out[n] = '\0';
  return true;
}

}  // namespace

const char* shmem_dir() {
#ifdef __QNXNTO__
  return "/dev/shmem";
#else
  const char* dir = getenv("NF_AA_SHMEM_DIR");
  return dir && dir[0] ? dir : "/dev/shm";
#endif
}

bool parse_state(const char* text, DaemonState* out) {
  int pid = 0, overlay = 0;
  char state[sizeof(out->state)];
  if (sscanf(text, "%23s %d %d", state, &pid, &overlay) != 3 || pid <= 0) return false;
  memcpy(out->state, state, sizeof(state));
  out->pid = static_cast<pid_t>(pid);
  out->overlay = overlay;
  return true;
}

bool read_state(DaemonState* out) {
  char text[64];
  return read_record(kStateFile, text, sizeof(text)) && parse_state(text, out);
}

const char* status_label(const char* record) {
  char state[24], carplay[4], wfd[4], usb[4], transport[12];
  if (!field(record, 0, state, sizeof(state)) || !field(record, 1, carplay, sizeof(carplay)) ||
      !field(record, 2, wfd, sizeof(wfd)) || !field(record, 3, usb, sizeof(usb)) ||
      !field(record, 4, transport, sizeof(transport))) {
    return NULL;
  }
  if (strcmp(state, kStateConnected) == 0) {
    if (strcmp(transport, kTransportWifi) == 0) return "Connected (Wi-Fi)";
    if (strcmp(transport, kTransportUsb) == 0) return "Connected (USB)";
    return "Connected";
  }
  if (strcmp(state, kStateConnecting) == 0) return "Connecting";
  if (strcmp(state, kStateWaiting) == 0) {
    /* No CarPlay hotspot and no Wi-Fi Direct group: no phone can join. */
    if (strcmp(carplay, "1") != 0 && strcmp(wfd, "1") != 0) {
      return strcmp(usb, "1") == 0 ? "No Wi-Fi: USB only" : "No Wi-Fi or USB";
    }
    return "Waiting for phone";
  }
  return NULL; /* a state this adapter does not know */
}

const char* current_status_label(const char* fallback) {
  char record[80];
  const char* label =
      read_record(kStatusFile, record, sizeof(record)) ? status_label(record) : NULL;
  return label ? label : fallback;
}

}  // namespace aa
