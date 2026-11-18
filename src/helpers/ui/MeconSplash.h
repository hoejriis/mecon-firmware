#pragma once
// The boot splash shared by the Companion and the Repeater (docs/UI.md): the MeshContinuum mark and wordmark, a rule, the
// project's address, and the MECON and MeshCore versions. No role and no posture. The mark is brand/mark-mono-white.svg of
// the MeshContinuum repository rendered at 32 px by tools/splash_preview.py (mark_c_array), the preview the owner chose.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace MeconSplash {

static const int MARK = 32;
// 32 rows of 4 bytes, most significant bit first: the order the displays' drawBitmap reads.
static const uint8_t mark_bits[MARK * MARK / 8] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x07, 0x80, 0x03, 0xe0, 0x07, 0xc0, 0x07, 0xf8, 0x1f, 0xe0,
  0x07, 0xfc, 0x3f, 0xe0, 0x0c, 0xc6, 0x63, 0x30, 0x0c, 0x03, 0xc0, 0x30, 0x08, 0x01, 0x80, 0x10,
  0x08, 0x01, 0x80, 0x10, 0x0c, 0x03, 0xc0, 0x30, 0x0c, 0xc6, 0x63, 0x30, 0x07, 0xfc, 0x3f, 0xe0,
  0x07, 0xf8, 0x1f, 0xe0, 0x03, 0xe0, 0x07, 0xc0, 0x01, 0xe0, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// "v1.18.0-mecon.0.3.7" -> mecon "0.3.7", meshcore "1.18.0". A version without the -mecon. part gives mecon "" and the
// whole string, less a leading v, as meshcore.
inline void splitVersion(const char* fw, char* mecon, size_t mcap, char* meshcore, size_t ccap) {
  if (*fw == 'v') fw++;
  const char* tag = strstr(fw, "-mecon.");
  if (tag) {
    snprintf(meshcore, ccap, "%.*s", (int)(tag - fw), fw);
    snprintf(mecon, mcap, "%s", tag + 7);
  } else {
    snprintf(meshcore, ccap, "%s", fw);
    if (mcap) mecon[0] = 0;
  }
}

// The three text lines below the rule, each cut to the 21 columns of a 128 px screen.
inline void lines(const char* fw, char url[22], char mecon[22], char meshcore[22]) {
  char m[48], c[48];
  splitVersion(fw, m, sizeof(m), c, sizeof(c));
  snprintf(url, 22, "%s", "https://mecon.cloud/");
  snprintf(mecon, 22, "MECON %s", m);
  snprintf(meshcore, 22, "MeshCore %s", c);
}

// Drawn with any of the firmware's display drivers (DisplayDriver.h): mark at the top left, wordmark beside it, a rule
// under both, then the three lines centred.
template <typename Display, typename Color>
void draw(Display& d, Color fg, const char* fw) {
  d.setColor(fg);
  d.drawXbm(0, 0, mark_bits, MARK, MARK);
  d.setTextSize(1);
  d.setCursor(MARK + 4, (MARK - 8) / 2);
  d.print("MeshContinuum");
  d.fillRect(0, MARK, d.width(), 1);
  char url[22], m[22], c[22];
  lines(fw, url, m, c);
  d.drawTextCentered(d.width() / 2, MARK + 3, url);
  d.drawTextCentered(d.width() / 2, MARK + 13, m);
  d.drawTextCentered(d.width() / 2, MARK + 23, c);
}

}  // namespace MeconSplash
