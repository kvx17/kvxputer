#include <Arduino.h>
#include <cstdlib>
#include "qrcode.h"
#include "qrencode.h"

int offsetsX;
int offsetsY;
int screenwidth;
int screenheight;
int multiply = 2;

static bool g_qrInverted = true;
static uint16_t g_qrFg = TFT_WHITE;
static uint16_t g_qrBg = TFT_BLACK;

QRcode::QRcode(tft_display *tft)
{
  this->tft = tft;
}

void QRcode::init()
{
  screenwidth = tft->width();
  screenheight = tft->height();
  int min = screenwidth;
  if (screenheight < screenwidth)
    min = screenheight;
  multiply = min / WD;
  offsetsX = (screenwidth - (WD * multiply)) / 2;
  offsetsY = (screenheight - (WD * multiply)) / 2;
}

void QRcode::render(int x, int y, int color)
{
  x = (x * multiply) + offsetsX;
  y = (y * multiply) + offsetsY;
  uint16_t c = (color == 1) ? g_qrFg : g_qrBg;
  if (multiply > 1)
  {
    tft->fillRect(x, y, multiply, multiply, c);
  }
  else
  {
    tft->drawPixel(x, y, c);
  }
}

void QRcode::create(String message)
{
  create(message, true);
}

void QRcode::create(String message, bool inverted)
{
  g_qrInverted = inverted;
  // inverted: dark bg + light modules; normal: light bg + dark modules
  if (g_qrInverted) {
    g_qrFg = TFT_WHITE;
    g_qrBg = TFT_BLACK;
  } else {
    g_qrFg = TFT_BLACK;
    g_qrBg = TFT_WHITE;
  }

  tft->fillScreen(g_qrBg);
  message.toCharArray((char *)strinbuf, 260);
  qrframe = (unsigned char *)malloc(600);
  if (!qrframe) return;
  qrencode();
  for (byte x = 0; x < WD; x += 2)
  {
    for (byte y = 0; y < WD; y++)
    {
      if (QRBIT(x, y) && QRBIT((x + 1), y))
      {
        render(x, y, 1);
        render((x + 1), y, 1);
      }
      if (!QRBIT(x, y) && QRBIT((x + 1), y))
      {
        render(x, y, 0);
        render((x + 1), y, 1);
      }
      if (QRBIT(x, y) && !QRBIT((x + 1), y))
      {
        render(x, y, 1);
        render((x + 1), y, 0);
      }
      if (!QRBIT(x, y) && !QRBIT((x + 1), y))
      {
        render(x, y, 0);
        render((x + 1), y, 0);
      }
    }
  }
  free(qrframe);
  qrframe = 0;
}
