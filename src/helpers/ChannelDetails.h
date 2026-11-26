#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
#ifndef MECON_NO_FAVOURITE_CLI   // the 256 KB STM32WL build has no screen or buzzer and no room: no field there, no mark ever set
  uint8_t favourite = 0;   // 1: this slot notifies the screen and buzzer (ChannelFavourite.h); off for every new slot
#endif
};
