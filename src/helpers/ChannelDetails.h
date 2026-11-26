#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
  uint8_t favourite = 0;   // 1: this slot notifies the screen and buzzer (ChannelFavourite.h); off for every new slot
};
