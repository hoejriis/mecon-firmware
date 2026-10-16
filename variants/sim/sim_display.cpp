#include "sim_display.h"
#include "sim_config.h"
#include "sim_clock.h"
#include <stdio.h>
#include <unistd.h>

ColorVal UIColor::window_bkg = 0, UIColor::title_bkg = 0, UIColor::title_txt = 1, UIColor::primary_txt = 1,
         UIColor::secondary_txt = 1, UIColor::warning_txt = 2, UIColor::popup_bkg = 0, UIColor::popup_txt = 1,
         UIColor::corp_blue = 1;

SimDisplay display;
SimButton user_btn;

static FILE* frames_file() {
  static FILE* f = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (!g_sim.ui_frames.empty()) {
      f = fopen(g_sim.ui_frames.c_str(), "a");
      if (f) { fprintf(f, "BOOT %d\n", (int)getpid()); fflush(f); }
    }
  }
  return f;
}

void SimDisplay::startFrame(ColorVal) { _frame.clear(); _size = 1; _x = _y = 0; }

void SimDisplay::print(const char* str) {
  char head[48];
  snprintf(head, sizeof head, "T %d %d %d ", _x, _y, _size);
  _frame += head;
  _frame += str;
  _frame += "\n";
  _x += getTextWidth(str);
}

// Consecutive identical frames are written once: the screen refreshes every second or so, and the file would
// grow without telling a test anything new.
void SimDisplay::endFrame() {
  FILE* f = frames_file();
  if (!f || _frame == _last) return;
  _last = _frame;
  fprintf(f, "FRAME %lu t=%lu\n%sEND\n", ++_frames, (unsigned long)millis(), _frame.c_str());
  fflush(f);
}

int SimButton::check(bool) {
  unsigned long now = millis();
  if (g_sim.ui_input.empty() || now < _next_poll) return BUTTON_EVENT_NONE;
  _next_poll = now + 20;
  FILE* f = fopen(g_sim.ui_input.c_str(), "r");
  if (!f) return BUTTON_EVENT_NONE;
  // The harness empties the input file before each boot, so the whole file belongs to this process. (Skipping to
  // the end on the first poll lost any gesture the test wrote before that poll, which a slow runner made common.)
  fseek(f, (long)_off, SEEK_SET);
  char line[32];
  int ev = BUTTON_EVENT_NONE;
  if (fgets(line, sizeof line, f)) {
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\n') {   // a complete line only: the test may be midway through writing it
      _off += n;
      if (!strncmp(line, "click", 5)) ev = BUTTON_EVENT_CLICK;
      else if (!strncmp(line, "long", 4)) ev = BUTTON_EVENT_LONG_PRESS;
      else if (!strncmp(line, "double", 6)) ev = BUTTON_EVENT_DOUBLE_CLICK;
      else if (!strncmp(line, "triple", 6)) ev = BUTTON_EVENT_TRIPLE_CLICK;
    }
  }
  fclose(f);
  if (ev != BUTTON_EVENT_NONE) {
    _next_poll = 0;
    fprintf(stderr, "SIMBTN ev=%d at=%lu off=%lu\n", ev, (unsigned long)millis(), (unsigned long)_off);
  }
  return ev;
}
