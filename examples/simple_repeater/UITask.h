#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>
#include <helpers/ui/FrontPage.h>

// Fills the front page's state from the mesh and the network (main.cpp knows them; broker is a buffer for its name).
typedef void (*FrontFill)(FrontPage::State& st, char* broker, size_t cap);

class UITask {
  mesh::MainBoard* _board;
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;
  FrontFill _front_fill = nullptr;
  uint8_t _page = 0;   // 0: the operational front page, 1: the stock radio page; a click moves on

  void renderCurrScreen();
  void renderHeader(uint8_t pages);
public:
  UITask(mesh::MainBoard& board, DisplayDriver& display) : _board(&board), _display(&display) { _next_read = _next_refresh = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);

  void setFrontFill(FrontFill fill) { _front_fill = fill; }

  void loop();
};