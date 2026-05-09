#include "EPD.h"
#include "EPDfont.h"

PAINT Paint;

void Paint_NewImage(uint8_t *image, uint16_t Width, uint16_t Height, uint16_t Rotate, uint16_t Color)
{
  Paint.Image        = image;
  Paint.widthMemory  = Width;
  Paint.heightMemory = Height;
  Paint.color        = Color;
  Paint.rotate       = Rotate;

  if (Rotate == ROTATE_0 || Rotate == ROTATE_180) {
    Paint.width  = Width;
    Paint.height = Height;
  } else {
    Paint.width  = Height;
    Paint.height = Width;
  }

  Paint.widthByte  = (Width  % 8 == 0) ? (Width  / 8) : (Width  / 8 + 1);
  Paint.heightByte = Height;
}

void Paint_Clear(uint8_t Color)
{
  for (uint16_t Y = 0; Y < Paint.heightByte; Y++)
    for (uint16_t X = 0; X < Paint.widthByte; X++)
      Paint.Image[X + Y * Paint.widthByte] = Color;
}

void Paint_SetPixel(uint16_t Xpoint, uint16_t Ypoint, uint16_t Color)
{
  uint16_t X, Y;
  uint32_t Addr;

  switch (Paint.rotate) {
    case ROTATE_0:
      X = Xpoint;
      Y = Ypoint;
      break;
    case ROTATE_90:
      X = Paint.widthMemory - Ypoint - 1;
      Y = Xpoint;
      break;
    case ROTATE_180:
      X = Paint.widthMemory  - Xpoint - 1;
      Y = Paint.heightMemory - Ypoint - 1;
      break;
    case ROTATE_270:
      X = Ypoint;
      Y = Paint.heightMemory - Xpoint - 1;
      break;
    default:
      return;
  }

  if (X >= Paint.widthMemory || Y >= Paint.heightMemory) return;

  Addr = X / 8 + Y * Paint.widthByte;
  if (Color == BLACK)
    Paint.Image[Addr] &= ~(0x80 >> (X % 8));  // 0 = black pixel
  else
    Paint.Image[Addr] |=  (0x80 >> (X % 8));  // 1 = white pixel
}

void EPD_ClearWindows(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color)
{
  for (uint16_t i = xs; i < xe; i++)
    for (uint16_t j = ys; j < ye; j++)
      Paint_SetPixel(i, j, color);
}

void EPD_DrawLine(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color)
{
  uint16_t Xpoint = Xstart, Ypoint = Ystart;
  int dx = abs((int)Xend - (int)Xstart);
  int dy = abs((int)Yend - (int)Ystart);
  int XAddway = Xstart < Xend ?  1 : -1;
  int YAddway = Ystart < Yend ?  1 : -1;
  int Esp = dx - dy;

  for (;;) {
    Paint_SetPixel(Xpoint, Ypoint, Color);
    if (Xpoint == Xend && Ypoint == Yend) break;
    int E2 = 2 * Esp;
    if (E2 >= -dy) { Esp -= dy; Xpoint += XAddway; }
    if (E2 <=  dx) { Esp += dx; Ypoint += YAddway; }
  }
}

void EPD_DrawRectangle(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,
                        uint16_t Color, uint8_t mode)
{
  if (mode) {
    for (uint16_t i = Ystart; i <= Yend; i++)
      EPD_DrawLine(Xstart, i, Xend, i, Color);
  } else {
    EPD_DrawLine(Xstart, Ystart, Xend,   Ystart, Color);
    EPD_DrawLine(Xstart, Ystart, Xstart, Yend,   Color);
    EPD_DrawLine(Xend,   Yend,   Xend,   Ystart, Color);
    EPD_DrawLine(Xend,   Yend,   Xstart, Yend,   Color);
  }
}

void EPD_DrawCircle(uint16_t X_Center, uint16_t Y_Center, uint16_t Radius,
                    uint16_t Color, uint8_t mode)
{
  int Esp;
  uint16_t XCurrent = 0, YCurrent = Radius;
  Esp = 3 - (Radius << 1);

  if (mode) {
    while (XCurrent <= YCurrent) {
      for (int sCountY = XCurrent; sCountY <= (int)YCurrent; sCountY++) {
        Paint_SetPixel(X_Center + XCurrent, Y_Center + sCountY, Color);
        Paint_SetPixel(X_Center - XCurrent, Y_Center + sCountY, Color);
        Paint_SetPixel(X_Center - sCountY,  Y_Center + XCurrent, Color);
        Paint_SetPixel(X_Center - sCountY,  Y_Center - XCurrent, Color);
        Paint_SetPixel(X_Center - XCurrent, Y_Center - sCountY, Color);
        Paint_SetPixel(X_Center + XCurrent, Y_Center - sCountY, Color);
        Paint_SetPixel(X_Center + sCountY,  Y_Center - XCurrent, Color);
        Paint_SetPixel(X_Center + sCountY,  Y_Center + XCurrent, Color);
      }
      if (Esp < 0) Esp += 4 * XCurrent + 6;
      else { Esp += 10 + 4 * (XCurrent - YCurrent); YCurrent--; }
      XCurrent++;
    }
  } else {
    while (XCurrent <= YCurrent) {
      Paint_SetPixel(X_Center + XCurrent, Y_Center + YCurrent, Color);
      Paint_SetPixel(X_Center - XCurrent, Y_Center + YCurrent, Color);
      Paint_SetPixel(X_Center - YCurrent, Y_Center + XCurrent, Color);
      Paint_SetPixel(X_Center - YCurrent, Y_Center - XCurrent, Color);
      Paint_SetPixel(X_Center - XCurrent, Y_Center - YCurrent, Color);
      Paint_SetPixel(X_Center + XCurrent, Y_Center - YCurrent, Color);
      Paint_SetPixel(X_Center + YCurrent, Y_Center - XCurrent, Color);
      Paint_SetPixel(X_Center + YCurrent, Y_Center + XCurrent, Color);
      if (Esp < 0) Esp += 4 * XCurrent + 6;
      else { Esp += 10 + 4 * (XCurrent - YCurrent); YCurrent--; }
      XCurrent++;
    }
  }
}

void EPD_ShowChar(uint16_t x, uint16_t y, uint16_t chr, uint16_t size1, uint16_t color)
{
  if (chr < ' ' || chr > '~') return;  // skip non-printable ASCII
  uint16_t i, m, temp, chr1;
  uint16_t x0 = x;
  uint16_t width = size1 / 2;
  // Row-major font data: ceil(width/8) bytes per row × height rows
  uint16_t size2 = ((width + 7) / 8) * size1;

  chr1 = chr - ' ';
  for (i = 0; i < size2; i++) {
    if      (size1 == 12) temp = ascii_1206[chr1][i];
    else if (size1 == 16) temp = ascii_1608[chr1][i];
    else if (size1 == 24) temp = ascii_2412[chr1][i];
    else if (size1 == 32) temp = ascii_3216[chr1][i];
    else if (size1 == 48) temp = ascii_4824[chr1][i];
    else return;

    for (m = 0; m < 8; m++) {
      if (temp & 0x01) Paint_SetPixel(x, y, color);
      temp >>= 1;
      x++;
      if ((x - x0) == width) {
        x = x0;
        y++;
        break;
      }
    }
  }
}

void EPD_ShowString(uint16_t x, uint16_t y, const char *chr, uint16_t size1, uint16_t color)
{
  while (*chr != '\0') {
    EPD_ShowChar(x, y, (uint16_t)*chr, size1, color);
    chr++;
    x += size1 / 2;
  }
}

static uint32_t mypow(uint8_t m, uint8_t n)
{
  uint32_t result = 1;
  while (n--) result *= m;
  return result;
}

void EPD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint16_t len, uint16_t size1, uint16_t color)
{
  uint8_t t, temp;
  uint8_t enshow = 0;
  uint16_t sizex = size1 / 2;

  for (t = 0; t < len; t++) {
    temp = (num / mypow(10, len - t - 1)) % 10;
    if (enshow == 0 && t < (len - 1)) {
      if (temp == 0) {
        EPD_ShowChar(x + t * sizex, y, ' ', size1, color);
        continue;
      } else {
        enshow = 1;
      }
    }
    EPD_ShowChar(x + t * sizex, y, temp + '0', size1, color);
  }
}

void EPD_ShowPicture(uint16_t x, uint16_t y, uint16_t sizex, uint16_t sizey,
                     const uint8_t BMP[], uint16_t Color)
{
  uint16_t i, j;
  uint32_t k = 0;
  uint8_t temp;

  for (i = 0; i < sizey; i++) {
    for (j = 0; j < sizex; j++) {
      if (j % 8 == 0) temp = BMP[k++];
      if (temp & 0x80) Paint_SetPixel(x + j, y + i,  Color);
      else             Paint_SetPixel(x + j, y + i, !Color);
      temp <<= 1;
    }
  }
}

void EPD_ShowFloatNum1(uint16_t x, uint16_t y, float num, uint8_t len, uint8_t pre,
                       uint8_t sizey, uint8_t color)
{
  uint8_t t, temp;
  uint16_t num1;
  uint8_t sizex = sizey / 2;
  num1 = (uint16_t)(num * mypow(10, pre));

  for (t = 0; t < len; t++) {
    temp = (num1 / mypow(10, len - t - 1)) % 10;
    if (t == (len - pre - 1)) {
      EPD_ShowChar(x + (len - pre - 1) * sizex, y, '.', sizey, color);
      t++;
      len++;
    }
    EPD_ShowChar(x + t * sizex, y, temp + '0', sizey, color);
  }
}

void EPD_ShowWatch(uint16_t x, uint16_t y, float num, uint8_t len, uint8_t pre,
                   uint8_t sizey, uint8_t color)
{
  uint8_t t, temp;
  uint16_t num1;
  uint8_t sizex = sizey / 2;
  num1 = (uint16_t)(num * mypow(10, pre));

  for (t = 0; t < len; t++) {
    temp = (num1 / mypow(10, len - t - 1)) % 10;
    if (t == (len - pre)) {
      EPD_ShowChar(x + (len - pre) * sizex + (sizex / 2 - 2), y - 6, ':', sizey, color);
      t++;
      len += 1;
    }
    EPD_ShowChar(x + t * sizex, y, temp + '0', sizey, color);
  }
}
