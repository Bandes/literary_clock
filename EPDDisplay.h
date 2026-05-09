#ifndef _EPD_DISPLAY_H_
#define _EPD_DISPLAY_H_

#include <Adafruit_GFX.h>
#include "EPD.h"

class EPDDisplay : public Adafruit_GFX {
public:
  EPDDisplay(int16_t w, int16_t h) : Adafruit_GFX(w, h) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    Paint_SetPixel((uint16_t)x, (uint16_t)y, color);
  }
};

#endif
