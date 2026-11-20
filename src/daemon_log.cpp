// SPDX-License-Identifier: MPL-2.0
#include "daemon_log.hpp"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daemon_status.hpp"
#include "protocol.hpp"

namespace aa {

namespace {

/* Reads up to `n` bytes at `offset` into buf. */
size_t read_at(int fd, off_t offset, char* buf, size_t n) {
  if (lseek(fd, offset, SEEK_SET) != offset) return 0;
  size_t got = 0;
  while (got < n) {
    const ssize_t r = read(fd, buf + got, n - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  return got;
}

/* Writes buf[0, n) and returns its last byte (or `last` when n is 0). */
char put(const char* buf, size_t n, FILE* out, char last) {
  if (n == 0) return last;
  fwrite(buf, 1, n, out);
  return buf[n - 1];
}

}  // namespace

bool copy_log_ends(const char* path, FILE* out, size_t head, size_t tail) {
  static char buf[kLogHeadBytes + kLogTailBytes];
  if (head + tail > sizeof(buf)) return false;
  const int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  struct stat st;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    close(fd);
    return false;
  }
  const size_t size = static_cast<size_t>(st.st_size);
  char last = '\n';
  if (size <= head + tail) {
    fprintf(out, "---- %s (%lu bytes) ----\n", path, static_cast<unsigned long>(size));
    last = put(buf, read_at(fd, 0, buf, size), out, last);
  } else {
    fprintf(out, "---- %s (%lu bytes): the first %lu and the last %lu ----\n", path,
            static_cast<unsigned long>(size), static_cast<unsigned long>(head),
            static_cast<unsigned long>(tail));
    /* Whole lines on both sides of the cut. */
    size_t n = read_at(fd, 0, buf, head);
    while (n > 0 && buf[n - 1] != '\n') --n;
    last = put(buf, n, out, last);
    fprintf(out, "---- ... ----\n");
    last = '\n';
    n = read_at(fd, static_cast<off_t>(size - tail), buf, tail);
    size_t skip = 0;
    while (skip < n && buf[skip] != '\n') ++skip;
    if (skip < n) ++skip;
    last = put(buf + skip, n - skip, out, last);
  }
  close(fd);
  fprintf(out, "%s---- end of %s ----\n", last == '\n' ? "" : "\n", path);
  fflush(out);
  return true;
}

void copy_daemon_log() {
  char path[256];
  if (snprintf(path, sizeof(path), "%s/%s", shmem_dir(), kLogFile) >= static_cast<int>(sizeof(path))) return;
  copy_log_ends(path, stdout);
}

}  // namespace aa
