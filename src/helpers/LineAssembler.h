#pragma once
#include <stddef.h>
#include <string.h>

// Assembles console lines, one byte at a time, into the caller's NUL-terminated buffer (the Ethernet console). CR or
// LF ends a line, and a LF that opens a line (the second half of CR+LF) is skipped. The buffer always stays
// terminated: a line longer than it is dropped up to its end and reported once, so its tail never runs as a command.
// After reset() (a new connection) the next byte starts an empty line, whatever the buffer held.
struct LineAssembler {
  enum Result { MORE, LINE, TOO_LONG };

  bool dropping = false;   // inside an over-long line, discarding up to its end
  bool fresh = false;      // the next byte starts an empty line

  void reset() { dropping = false; fresh = true; }

  // LINE: buf holds the line, without its end. TOO_LONG: a line was dropped, buf is empty. MORE: keep feeding.
  Result feed(char* buf, size_t size, char c) {
    if (size < 2) return MORE;
    if (fresh) { buf[0] = 0; fresh = false; }
    const size_t len = strnlen(buf, size - 1);
    if (c == '\r' || c == '\n') {
      if (dropping) { dropping = false; buf[0] = 0; return TOO_LONG; }
      if (c == '\n' && len == 0) return MORE;
      buf[len] = 0;
      return LINE;
    }
    if (dropping) return MORE;
    if (len >= size - 1) { dropping = true; buf[0] = 0; return MORE; }
    buf[len] = c;
    buf[len + 1] = 0;
    return MORE;
  }
};
