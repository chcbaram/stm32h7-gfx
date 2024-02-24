#include "lcd.h"


#ifdef _USE_HW_LCD
#include "ltdc.h"
#include "cli.h"
#include "gpio.h"
#include "resize.h"
#include "hangul/han.h"
#include "lcd/lcd_fonts.h"
#include "lcd/st7701.h"
#include "nvs.h"
#include "touch.h"
#include "pdm.h"
#include "mem.h"
#include "button.h"
#include "i2s.h"

#ifdef _USE_HW_PWM
#include "pwm.h"
#endif

#ifdef _USE_HW_ADC
#include "adc.h"
#endif

#ifndef _swap_int16_t
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#endif



#define LCD_FONT_RESIZE_WIDTH  64

#define MAKECOL(r, g, b) ( ((r)<<11) | ((g)<<5) | (b))




#define LCD_OPT_DEF   __attribute__((optimize("O2")))
#define LCD_CFG_NAME "cfg.lcd"

typedef struct
{
  int16_t x;
  int16_t y;
} lcd_pixel_t;

typedef struct
{
  uint8_t backlight_value;
} lcd_cfg_t;


static void disHanFont(int x, int y, han_font_t *FontPtr, uint16_t textcolor);
static void disEngFont(int x, int y, char ch, lcd_font_t *font, uint16_t textcolor);
static void lcdDrawLineBuffer(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, lcd_pixel_t *line);
static void lcdTransferDoneISR(void);
static void lcdFillBuffer(void * pDst, uint32_t xSize, uint32_t ySize, uint32_t OffLine, uint32_t ColorIndex);
static bool lcdLoadCfg(void);
static bool lcdSaveCfg(void);

#ifdef _USE_HW_CLI
static void cliLcd(cli_args_t *args);
#endif



static bool is_init = false;
static volatile bool is_tx_done = true;
static uint8_t backlight_value = 100;
static LcdFont lcd_font = LCD_FONT_HAN;


static volatile uint32_t fps_pre_time;
static volatile uint32_t fps_time;
static volatile uint32_t fps_count = 0;

static volatile int32_t draw_fps = -1;
static volatile uint32_t draw_pre_time = 0;
static volatile uint32_t draw_frame_time = 0;
static LcdResizeMode lcd_resize_mode = LCD_RESIZE_NEAREST;

static uint16_t *p_draw_frame_buf = NULL;
static uint16_t __attribute__((aligned(64))) font_src_buffer[16 * 16];
static uint16_t __attribute__((aligned(64))) font_dst_buffer[LCD_FONT_RESIZE_WIDTH * LCD_FONT_RESIZE_WIDTH];

static lcd_font_t *font_tbl[LCD_FONT_MAX] = { &font_07x10, &font_11x18, &font_16x26, &font_hangul};
static lcd_cfg_t lcd_cfg;
static void (*draw_callback)(void) = NULL;

#if HW_LCD_LOGO == 1
static bool is_logo_on = false;
LVGL_IMG_DEF(logo_img);
#endif






bool lcdInit(void)
{
  bool ret = true;

  backlight_value = 100;

  ret &= ltdcInit();
  ret &= st7701Init();
  is_init = ret;

  lcdLoadCfg();
  lcdSetBackLight(backlight_value);

  logPrintf("[%s] lcdInit()\n", is_init ? "OK":"NG");

  p_draw_frame_buf = ltdcGetFrameBuffer();

  if (is_init != true)
  {
    return false;
  }

  #if HW_LCD_LOGO > 0
  lcdLogoOn();
  #endif

#ifdef _USE_HW_CLI
  cliAdd("lcd", cliLcd);
#endif

  return true;
}

void lcdTransferDoneISR(void)
{
  fps_time = millis() - fps_pre_time;
  fps_pre_time = millis();
  draw_frame_time = millis() - draw_pre_time;

  if (fps_time > 0)
  {
    fps_count = 1000 / fps_time;
  }
}

void lcdSetCallbackDraw(void (*func)(void))
{
  draw_callback = func;
}

bool lcdLoadCfg(void)
{
  bool ret = true;

  if (nvsGet(LCD_CFG_NAME, &lcd_cfg, sizeof(lcd_cfg)) == true)
  {
    backlight_value = lcd_cfg.backlight_value;
  }
  else
  {
    lcd_cfg.backlight_value = backlight_value;
    ret = nvsSet(LCD_CFG_NAME, &lcd_cfg, sizeof(lcd_cfg));
  }
  return ret;
}

bool lcdSaveCfg(void)
{
  bool ret = true;

  lcd_cfg.backlight_value = backlight_value;
  ret = nvsSet(LCD_CFG_NAME, &lcd_cfg, sizeof(lcd_cfg));

  return ret;
}

uint32_t lcdGetDrawTime(void)
{
  return draw_frame_time;
}

bool lcdIsInit(void)
{
  return is_init;
}

void lcdReset(void)
{
}

uint8_t lcdGetBackLight(void)
{
  return backlight_value;
}

void lcdSetBackLight(uint8_t value)
{
  value = constrain((int8_t)value, 0, 100);

  if (value != backlight_value)
  {
    backlight_value = value;
  }

  lcdSaveCfg();

#ifdef _USE_HW_PWM
  pwmWrite(0, cmap(value, 0, 100, 0, pwmGetMax(0)));
#else
  if (backlight_value > 0)
  {
    gpioPinWrite(_PIN_DEF_BL_CTL, _DEF_HIGH);
  }
  else
  {
    gpioPinWrite(_PIN_DEF_BL_CTL, _DEF_LOW);
  }
#endif
}

LCD_OPT_DEF uint32_t lcdReadPixel(uint16_t x_pos, uint16_t y_pos)
{
  return p_draw_frame_buf[y_pos * LCD_WIDTH + x_pos];
}

LCD_OPT_DEF inline void lcdDrawPixel(int16_t x_pos, int16_t y_pos, uint32_t rgb_code)
{
  #if HW_LCD_SWAP_RGB > 0
  uint16_t rgb_out;

  if (x_pos < 0 || x_pos >= LCD_WIDTH) return;
  if (y_pos < 0 || y_pos >= LCD_HEIGHT) return;

  rgb_out  = (rgb_code>>8) & 0x00FF;
  rgb_out |= (rgb_code<<8) & 0xFF00;

  p_draw_frame_buf[y_pos * LCD_WIDTH + x_pos] = rgb_code;
  #else
  if (x_pos < 0 || x_pos >= LCD_WIDTH) return;
  if (y_pos < 0 || y_pos >= LCD_HEIGHT) return;

  p_draw_frame_buf[y_pos * LCD_WIDTH + x_pos] = rgb_code;  
  #endif
}

LCD_OPT_DEF void lcdClear(uint32_t rgb_code)
{
  lcdClearBuffer(rgb_code);

  lcdUpdateDraw();
}

LCD_OPT_DEF void lcdClearBuffer(uint32_t rgb_code)
{
  uint16_t *p_buf = lcdGetFrameBuffer();
  uint16_t color;

  color = rgb_code;

  #if 0
  for (int i=0; i<LCD_WIDTH * LCD_HEIGHT; i++)
  {
    p_buf[i] = color;
  }
  #else
  lcdFillBuffer(p_buf, LCD_WIDTH, LCD_HEIGHT, 0, color);
  #endif
}

LCD_OPT_DEF void lcdDrawFillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color)
{
  int32_t  x  = 0;
  int32_t  dx = 1;
  int32_t  dy = r+r;
  int32_t  p  = -(r>>1);


  lcdDrawHLine(x0 - r, y0, dy+1, color);

  while(x<r)
  {

    if(p>=0) 
    {
      dy-=2;
      p-=dy;
      r--;
    }

    dx+=2;
    p+=dx;

    x++;

    lcdDrawHLine(x0 - r, y0 + x, 2 * r+1, color);
    lcdDrawHLine(x0 - r, y0 - x, 2 * r+1, color);
    lcdDrawHLine(x0 - x, y0 + r, 2 * x+1, color);
    lcdDrawHLine(x0 - x, y0 - r, 2 * x+1, color);
  }
}

LCD_OPT_DEF void lcdDrawCircleHelper( int32_t x0, int32_t y0, int32_t r, uint8_t cornername, uint32_t color)
{
  int32_t f     = 1 - r;
  int32_t ddF_x = 1;
  int32_t ddF_y = -2 * r;
  int32_t x     = 0;

  while (x < r)
  {
    if (f >= 0)
    {
      r--;
      ddF_y += 2;
      f     += ddF_y;
    }
    x++;
    ddF_x += 2;
    f     += ddF_x;
    if (cornername & 0x4)
    {
      lcdDrawPixel(x0 + x, y0 + r, color);
      lcdDrawPixel(x0 + r, y0 + x, color);
    }
    if (cornername & 0x2)
    {
      lcdDrawPixel(x0 + x, y0 - r, color);
      lcdDrawPixel(x0 + r, y0 - x, color);
    }
    if (cornername & 0x8)
    {
      lcdDrawPixel(x0 - r, y0 + x, color);
      lcdDrawPixel(x0 - x, y0 + r, color);
    }
    if (cornername & 0x1)
    {
      lcdDrawPixel(x0 - r, y0 - x, color);
      lcdDrawPixel(x0 - x, y0 - r, color);
    }
  }
}

LCD_OPT_DEF void lcdDrawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
  // smarter version
  lcdDrawHLine(x + r    , y        , w - r - r, color); // Top
  lcdDrawHLine(x + r    , y + h - 1, w - r - r, color); // Bottom
  lcdDrawVLine(x        , y + r    , h - r - r, color); // Left
  lcdDrawVLine(x + w - 1, y + r    , h - r - r, color); // Right

  // draw four corners
  lcdDrawCircleHelper(x + r        , y + r        , r, 1, color);
  lcdDrawCircleHelper(x + w - r - 1, y + r        , r, 2, color);
  lcdDrawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
  lcdDrawCircleHelper(x + r        , y + h - r - 1, r, 8, color);
}

LCD_OPT_DEF void lcdDrawFillCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, int32_t delta, uint32_t color)
{
  int32_t f     = 1 - r;
  int32_t ddF_x = 1;
  int32_t ddF_y = -r - r;
  int32_t y     = 0;

  delta++;

  while (y < r)
  {
    if (f >= 0)
    {
      r--;
      ddF_y += 2;
      f     += ddF_y;
    }

    y++;
    ddF_x += 2;
    f     += ddF_x;

    if (cornername & 0x1)
    {
      lcdDrawHLine(x0 - r, y0 + y, r + r + delta, color);
      lcdDrawHLine(x0 - y, y0 + r, y + y + delta, color);
    }
    if (cornername & 0x2)
    {
      lcdDrawHLine(x0 - r, y0 - y, r + r + delta, color); // 11995, 1090
      lcdDrawHLine(x0 - y, y0 - r, y + y + delta, color);
    }
  }
}

LCD_OPT_DEF void lcdDrawFillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
  // smarter version
  lcdDrawFillRect(x, y + r, w, h - r - r, color);

  // draw four corners
  lcdDrawFillCircleHelper(x + r, y + h - r - 1, r, 1, w - r - r - 1, color);
  lcdDrawFillCircleHelper(x + r, y + r        , r, 2, w - r - r - 1, color);
}

LCD_OPT_DEF void lcdDrawTriangle(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint32_t color)
{
  lcdDrawLine(x1, y1, x2, y2, color);
  lcdDrawLine(x1, y1, x3, y3, color);
  lcdDrawLine(x2, y2, x3, y3, color);
}

LCD_OPT_DEF void lcdDrawFillTriangle(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint32_t color)
{
  uint16_t max_line_size_12 = cmax(abs(x1-x2), abs(y1-y2));
  uint16_t max_line_size_13 = cmax(abs(x1-x3), abs(y1-y3));
  uint16_t max_line_size_23 = cmax(abs(x2-x3), abs(y2-y3));
  uint16_t max_line_size = max_line_size_12;
  uint16_t i = 0;

  if (max_line_size_13 > max_line_size)
  {
    max_line_size = max_line_size_13;
  }
  if (max_line_size_23 > max_line_size)
  {
    max_line_size = max_line_size_23;
  }

  lcd_pixel_t line[max_line_size];

  lcdDrawLineBuffer(x1, y1, x2, y2, color, line);
  for (i = 0; i < max_line_size_12; i++)
  {
    lcdDrawLine(x3, y3, line[i].x, line[i].y, color);
  }
  lcdDrawLineBuffer(x1, y1, x3, y3, color, line);
  for (i = 0; i < max_line_size_13; i++)
  {
    lcdDrawLine(x2, y2, line[i].x, line[i].y, color);
  }
  lcdDrawLineBuffer(x2, y2, x3, y3, color, line);
  for (i = 0; i < max_line_size_23; i++)
  {
    lcdDrawLine(x1, y1, line[i].x, line[i].y, color);
  }
}

void lcdSetFps(int32_t fps)
{
  draw_fps = fps;
}

uint32_t lcdGetFps(void)
{
  return fps_count;
}

uint32_t lcdGetFpsTime(void)
{
  return fps_time;
}

bool lcdDrawAvailable(void)
{
  bool ret;

  ret = ltdcDrawAvailable();
  p_draw_frame_buf = ltdcGetFrameBuffer();
  draw_pre_time = millis();
  return ret;
}

bool lcdRequestDraw(void)
{
  if (is_init != true)
  {
    return false;
  }

  lcdTransferDoneISR();

  ltdcRequestDraw();
  return true;
}

void lcdUpdateDraw(void)
{
  uint32_t pre_time;

  if (is_init != true)
  {
    return;
  }

  lcdRequestDraw();

  pre_time = millis();
  while(lcdDrawAvailable() != true)
  {
    delay(1);
    if (millis()-pre_time >= 100)
    {
      break;
    }
  }
}

void lcdSetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  if (is_init != true)
  {
    return;
  }
}

uint16_t *lcdGetFrameBuffer(void)
{
  return ltdcGetFrameBuffer();
}

uint16_t *lcdGetCurrentFrameBuffer(void)
{
  return ltdcGetCurrentFrameBuffer();
}

void lcdDisplayOff(void)
{
}

void lcdDisplayOn(void)
{
  lcdSetBackLight(lcdGetBackLight());
}

int32_t lcdGetWidth(void)
{
  return LCD_WIDTH;
}

int32_t lcdGetHeight(void)
{
  return LCD_HEIGHT;
}

void lcdSetRotation(uint8_t mode)
{
  // lcd.setRotation(mode);
  // lcdSaveCfg();
}

void lcdDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
  int16_t steep = abs(y1 - y0) > abs(x1 - x0);

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;


  if (steep)
  {
    _swap_int16_t(x0, y0);
    _swap_int16_t(x1, y1);
  }

  if (x0 > x1)
  {
    _swap_int16_t(x0, x1);
    _swap_int16_t(y0, y1);
  }

  int16_t dx, dy;
  dx = x1 - x0;
  dy = abs(y1 - y0);

  int16_t err = dx / 2;
  int16_t ystep;

  if (y0 < y1)
  {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0<=x1; x0++)
  {
    if (steep)
    {
      lcdDrawPixel(y0, x0, color);
    } else
    {
      lcdDrawPixel(x0, y0, color);
    }
    err -= dy;
    if (err < 0)
    {
      y0 += ystep;
      err += dx;
    }
  }
}

void lcdDrawLineBuffer(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, lcd_pixel_t *line)
{
  int16_t steep = abs(y1 - y0) > abs(x1 - x0);

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;


  if (steep)
  {
    _swap_int16_t(x0, y0);
    _swap_int16_t(x1, y1);
  }

  if (x0 > x1)
  {
    _swap_int16_t(x0, x1);
    _swap_int16_t(y0, y1);
  }

  int16_t dx, dy;
  dx = x1 - x0;
  dy = abs(y1 - y0);

  int16_t err = dx / 2;
  int16_t ystep;

  if (y0 < y1)
  {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0<=x1; x0++)
  {
    if (steep)
    {
      if (line != NULL)
      {
        line->x = y0;
        line->y = x0;
      }
      lcdDrawPixel(y0, x0, color);
    } else
    {
      if (line != NULL)
      {
        line->x = x0;
        line->y = y0;
      }
      lcdDrawPixel(x0, y0, color);
    }
    if (line != NULL)
    {
      line++;
    }
    err -= dy;
    if (err < 0)
    {
      y0 += ystep;
      err += dx;
    }
  }
}

void lcdDrawVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
  lcdDrawLine(x, y, x, y+h-1, color);
}

void lcdDrawHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
  lcdDrawLine(x, y, x+w-1, y, color);
}

void lcdDrawFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  for (int16_t i=x; i<x+w; i++)
  {
    lcdDrawVLine(i, y, h, color);
  }
}

void lcdDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  lcdDrawHLine(x, y, w, color);
  lcdDrawHLine(x, y+h-1, w, color);
  lcdDrawVLine(x, y, h, color);
  lcdDrawVLine(x+w-1, y, h, color);
}

void lcdDrawFillScreen(uint16_t color)
{
  lcdDrawFillRect(0, 0, HW_LCD_WIDTH, HW_LCD_HEIGHT, color);
}

void lcdFillBuffer(void * pDst, uint32_t xSize, uint32_t ySize, uint32_t OffLine, uint32_t ColorIndex)
{
  /* Set up mode */
  DMA2D->CR      = 0x00030000UL | (1 << 9);
  DMA2D->OCOLR   = ColorIndex;

  /* Set up pointers */
  DMA2D->OMAR    = (uint32_t)pDst;

  /* Set up offsets */
  DMA2D->OOR     = OffLine;

  /* Set up pixel format */
  DMA2D->OPFCCR  = LTDC_PIXEL_FORMAT_RGB565;

  /*  Set up size */
  DMA2D->NLR     = (uint32_t)(xSize << 16) | (uint32_t)ySize;

  DMA2D->CR     |= DMA2D_CR_START;

  /* Wait until transfer is done */
  while (DMA2D->CR & DMA2D_CR_START)
  {
  }
}

void lcdPrintf(int x, int y, uint16_t color,  const char *fmt, ...)
{
  va_list arg;
  va_start (arg, fmt);
  int32_t len;
  char print_buffer[256];
  int Size_Char;
  int i, x_Pre = x;
  han_font_t FontBuf;
  uint8_t font_width;
  uint8_t font_height;


  len = vsnprintf(print_buffer, 255, fmt, arg);
  va_end (arg);

  if (font_tbl[lcd_font]->data != NULL)
  {
    for( i=0; i<len; i+=Size_Char )
    {
      disEngFont(x, y, print_buffer[i], font_tbl[lcd_font], color);

      Size_Char = 1;
      font_width = font_tbl[lcd_font]->width;
      font_height = font_tbl[lcd_font]->height;
      x += font_width;

      if ((x+font_width) > HW_LCD_WIDTH)
      {
        x  = x_Pre;
        y += font_height;
      }
    }
  }
  else
  {
    for( i=0; i<len; i+=Size_Char )
    {
      hanFontLoad( &print_buffer[i], &FontBuf );

      disHanFont( x, y, &FontBuf, color);

      Size_Char = FontBuf.Size_Char;
      if (Size_Char >= 2)
      {
        font_width = 16;
        x += 2*8;
      }
      else
      {
        font_width = 8;
        x += 1*8;
      }

      if ((x+font_width) > HW_LCD_WIDTH)
      {
        x  = x_Pre;
        y += 16;
      }

      if( FontBuf.Code_Type == PHAN_END_CODE ) break;
    }
  }
}


uint32_t lcdGetStrWidth(const char *fmt, ...)
{
  va_list arg;
  va_start (arg, fmt);
  int32_t len;
  char print_buffer[256];
  int Size_Char;
  int i;
  han_font_t FontBuf;
  uint32_t str_len;


  len = vsnprintf(print_buffer, 255, fmt, arg);
  va_end (arg);


  str_len = 0;

  for( i=0; i<len; i+=Size_Char )
  {
    hanFontLoad( &print_buffer[i], &FontBuf );

    Size_Char = FontBuf.Size_Char;

    if (Size_Char >= 2)
    {
      str_len += (2 * 8);
    }
    else
    {
      str_len += (1 * 8);
    }
    if( FontBuf.Code_Type == PHAN_END_CODE ) break;
  }

  return str_len;
}

void disHanFont(int x, int y, han_font_t *FontPtr, uint16_t textcolor)
{
  uint16_t    i, j, Loop;
  uint16_t  FontSize = FontPtr->Size_Char;
  uint16_t index_x;

  if (FontSize > 2)
  {
    FontSize = 2;
  }

  for ( i = 0 ; i < 16 ; i++ )        // 16 Lines per Font/Char
  {
    index_x = 0;
    for ( j = 0 ; j < FontSize ; j++ )      // 16 x 16 (2 Bytes)
    {
      uint8_t font_data;

      font_data = FontPtr->FontBuffer[i*FontSize +j];

      for( Loop=0; Loop<8; Loop++ )
      {
        if( (font_data<<Loop) & (0x80))
        {
          lcdDrawPixel(x + index_x, y + i, textcolor);
        }
        index_x++;
      }
    }
  }
}

void disEngFont(int x, int y, char ch, lcd_font_t *font, uint16_t textcolor)
{
  uint32_t i, b, j;


  // We gaan door het font
  for (i = 0; i < font->height; i++)
  {
    b = font->data[(ch - 32) * font->height + i];
    for (j = 0; j < font->width; j++)
    {
      if ((b << j) & 0x8000)
      {
        lcdDrawPixel(x + j, (y + i), textcolor);
      }
    }
  }
}

void lcdSetFont(LcdFont font)
{
  lcd_font = font;
}

LcdFont lcdGetFont(void)
{
  return lcd_font;
}

LCD_OPT_DEF void lcdDrawPixelBuffer(int16_t x_pos, int16_t y_pos, uint32_t rgb_code)
{
  font_src_buffer[y_pos * 16 + x_pos] = rgb_code;
}

void disHanFontBuffer(int x, int y, han_font_t *FontPtr, uint16_t textcolor)
{
  uint16_t    i, j, Loop;
  uint16_t  FontSize = FontPtr->Size_Char;
  uint16_t index_x;

  if (FontSize > 2)
  {
    FontSize = 2;
  }

  if (textcolor == 0)
  {
    textcolor = 1;
  }
  for ( i = 0 ; i < 16 ; i++ )        // 16 Lines per Font/Char
  {
    index_x = 0;
    for ( j = 0 ; j < FontSize ; j++ )      // 16 x 16 (2 Bytes)
    {
      uint8_t font_data;

      font_data = FontPtr->FontBuffer[i*FontSize +j];

      for( Loop=0; Loop<8; Loop++ )
      {
        if(font_data & ((uint8_t)0x80>>Loop))
        {
          lcdDrawPixelBuffer(index_x, i, textcolor);
        }
        else
        {
          lcdDrawPixelBuffer(index_x, i, 0);
        }
        index_x++;
      }
    }
  }
}

LCD_OPT_DEF uint16_t lcdGetColorMix(uint16_t c1, uint16_t c2, uint8_t mix)
{
  uint16_t r, g, b;
  uint16_t ret;


  r = ((uint16_t)((uint16_t) GETR(c1) * mix + GETR(c2) * (255 - mix)) >> 8);
  g = ((uint16_t)((uint16_t) GETG(c1) * mix + GETG(c2) * (255 - mix)) >> 8);
  b = ((uint16_t)((uint16_t) GETB(c1) * mix + GETB(c2) * (255 - mix)) >> 8);

  ret = MAKECOL(r, g, b);

  return ret;
}

LCD_OPT_DEF void lcdDrawPixelMix(int16_t x_pos, int16_t y_pos, uint32_t rgb_code, uint8_t mix)
{
  uint16_t color1, color2;

  if (x_pos < 0 || x_pos >= LCD_WIDTH) return;
  if (y_pos < 0 || y_pos >= LCD_HEIGHT) return;


  color1 = p_draw_frame_buf[y_pos * LCD_WIDTH + x_pos];
  color2 = rgb_code;

  p_draw_frame_buf[y_pos * LCD_WIDTH + x_pos] = lcdGetColorMix(color1, color2, 255-mix);
}

void lcdPrintfResize(int x, int y, uint16_t color,  float ratio_h, const char *fmt, ...)
{
  va_list arg;
  va_start (arg, fmt);
  int32_t len;
  char print_buffer[256];
  int Size_Char;
  int i;
  int x_Pre = x;
  int y_Pre = y;
  han_font_t FontBuf;
  uint8_t font_width;
  resize_image_t r_src, r_dst;
  uint16_t pixel;
  int16_t x_pos;
  int16_t y_pos;
  float ratio;

  r_src.x = 0;
  r_src.y = 0;
  r_src.w = 0;
  r_src.h = 16;
  r_src.stride = 16;
  r_src.p_data = font_src_buffer;


  len = vsnprintf(print_buffer, 255, fmt, arg);
  va_end (arg);

  if (ratio_h > LCD_FONT_RESIZE_WIDTH)
  {
    ratio_h = LCD_FONT_RESIZE_WIDTH;
  }
  ratio = ratio_h / 16;

  x = 0;
  y = 0;
  for( i=0; i<len; i+=Size_Char )
  {
    hanFontLoad( &print_buffer[i], &FontBuf );


    disHanFontBuffer(x, y, &FontBuf, 0xFF);

    x_pos = x_Pre + x * ratio;
    y_pos = y_Pre + y * ratio;

    Size_Char = FontBuf.Size_Char;
    if (Size_Char >= 2)
    {
      font_width = 16;
      x += 2*8;
    }
    else
    {
      font_width = 8;
      x += 1*8;
    }

    r_src.w = font_width;

    //if ((x+font_width) > HW_LCD_WIDTH)
    if ((x_pos + font_width*ratio) >= HW_LCD_WIDTH)
    {
      x  = x_Pre;
      y += 16;

      x_pos = x_Pre + x * ratio;
      y_pos = y_Pre + y * ratio;
    }

    r_dst.x = 0;
    r_dst.y = 0;
    r_dst.w = r_src.w * ratio;
    r_dst.h = r_src.h * ratio;
    r_dst.stride = LCD_FONT_RESIZE_WIDTH;
    r_dst.p_data = font_dst_buffer;

    if (r_dst.w == 0) r_dst.w = 1;
    if (r_dst.h == 0) r_dst.h = 1;

    if (lcd_resize_mode == LCD_RESIZE_BILINEAR)
    {
      resizeImageFastGray(&r_src, &r_dst);
    }
    else
    {
      resizeImageNearest(&r_src, &r_dst);
    }


    for (int i_y=0; i_y<r_dst.h; i_y++)
    {
      for (int i_x=0; i_x<r_dst.w; i_x++)
      {
        pixel = font_dst_buffer[(i_y+r_dst.y)*LCD_FONT_RESIZE_WIDTH + i_x];
        if (pixel > 0)
        {
          lcdDrawPixelMix(x_pos+i_x, y_pos+i_y, color, pixel);
        }
      }
    }


    if( FontBuf.Code_Type == PHAN_END_CODE ) break;
  }
}

void lcdPrintfRect(int x, int y, int w, int h, uint16_t color, float ratio_h, uint16_t align, const char *fmt, ...)
{
  va_list arg;
  int32_t len;
  char print_buffer[256];
  int Size_Char;
  int i;
  int x_Pre = x;
  int y_Pre = y;
  han_font_t FontBuf;
  uint8_t font_width;
  resize_image_t r_src, r_dst;
  uint16_t pixel;
  int16_t x_pos;
  int16_t y_pos;
  uint32_t str_width;
  float ratio;

  r_src.x = 0;
  r_src.y = 0;
  r_src.w = 0;
  r_src.h = 16;
  r_src.stride = 16;
  r_src.p_data = font_src_buffer;


  va_start (arg, fmt);
  len = vsnprintf(print_buffer, 255, fmt, arg);
  va_end (arg);
  
  if (ratio_h > LCD_FONT_RESIZE_WIDTH)
  {
    ratio_h = LCD_FONT_RESIZE_WIDTH;
  }
  ratio = ratio_h / 16;


  str_width = lcdGetStrWidth(print_buffer) * ratio;

  x = 0;
  y = 0;
  for( i=0; i<len; i+=Size_Char )
  {
    hanFontLoad( &print_buffer[i], &FontBuf );


    disHanFontBuffer(0, 0, &FontBuf, 0xFF);

    x_pos = x_Pre + x * ratio;
    y_pos = y_Pre + y * ratio;

    Size_Char = FontBuf.Size_Char;
    if (Size_Char >= 2)
    {
      font_width = 16;
      x += 2*8;
    }
    else
    {
      font_width = 8;
      x += 1*8;
    }

    r_src.w = font_width;

    if ((x_pos + font_width*ratio) >= HW_LCD_WIDTH)
    {
      x  = x_Pre;
      y += 16;

      x_pos = x_Pre + x * ratio;
      y_pos = y_Pre + y * ratio;
    }

    r_dst.x = 0;
    r_dst.y = 0;
    r_dst.w = r_src.w * ratio;
    r_dst.h = r_src.h * ratio;
    r_dst.stride = LCD_FONT_RESIZE_WIDTH;
    r_dst.p_data = font_dst_buffer;

    if (r_dst.w == 0) r_dst.w = 1;
    if (r_dst.h == 0) r_dst.h = 1;

    if (lcd_resize_mode == LCD_RESIZE_BILINEAR)
    {
      resizeImageFastGray(&r_src, &r_dst);
    }
    else
    {
      resizeImageNearest(&r_src, &r_dst);
    }

    int x_o = 0;
    int y_o = 0;

    if (w > str_width)
    {
      if (align & LCD_ALIGN_H_CENTER)
      {
        x_o += (w-str_width)/2;
      }
      if (align & LCD_ALIGN_H_RIGHT)
      {
        x_o += (w-str_width);
      }
    }
    if (h > r_dst.h)
    {
      if (align & LCD_ALIGN_V_CENTER)
      {
        y_o += (h-r_dst.h)/2 + 0;
      }
      if (align & LCD_ALIGN_V_BOTTOM)
      {
        y_o += (h-r_dst.h);
      }
    }


    for (int i_y=0; i_y<r_dst.h; i_y++)
    {
      for (int i_x=0; i_x<r_dst.w; i_x++)
      {
        pixel = font_dst_buffer[(i_y+r_dst.y)*LCD_FONT_RESIZE_WIDTH + i_x];
        if (pixel > 0)
        {
          lcdDrawPixelMix(x_o+x_pos+i_x, y_o+y_pos+i_y, color, pixel);
        }
      }
    }


    if( FontBuf.Code_Type == PHAN_END_CODE ) break;
  }
}

void lcdSetResizeMode(LcdResizeMode mode)
{
  lcd_resize_mode = mode;
}

#if HW_LCD_LVGL == 1
image_t lcdCreateImage(lvgl_img_t *p_lvgl, int16_t x, int16_t y, int16_t w, int16_t h)
{
  image_t ret;

  ret.x = x;
  ret.y = y;

  if (w > 0) ret.w = w;
  else       ret.w = p_lvgl->header.w;

  if (h > 0) ret.h = h;
  else       ret.h = p_lvgl->header.h;

  ret.p_img = p_lvgl;

  return ret;
}

LCD_OPT_DEF void lcdDrawImage(image_t *p_img, int16_t draw_x, int16_t draw_y)
{
  int32_t o_x;
  int32_t o_y;  
  int16_t o_w;
  int16_t o_h;
  const uint16_t *p_data;
  uint16_t pixel;
  int16_t img_x = 0;
  int16_t img_y = 0;
  int16_t img_w = 0;
  int16_t img_h = 0;

  o_w = p_img->w;
  o_h = p_img->h;

  if (img_w > 0) o_w = img_w;
  if (img_h > 0) o_h = img_h;

  p_data = (uint16_t *)p_img->p_img->data;

  for (int yi=0; yi<o_h; yi++)
  {
    o_y = (p_img->y + yi + img_y);
    if (o_y >= p_img->p_img->header.h) break;

    o_y = o_y * p_img->p_img->header.w;
    for (int xi=0; xi<o_w; xi++)
    {
      o_x = p_img->x + xi + img_x;
      if (o_x >= p_img->p_img->header.w) break;

      pixel = p_data[o_y + o_x];
      if (pixel != green)
      {
        lcdDrawPixel(draw_x+xi, draw_y+yi, pixel);
      }
    }
  }  
}

#if 0
void lcdLogoOn(void)
{
  image_t logo;
  int16_t x, y;
  
  logo = lcdCreateImage(&logo_img, 0, 0, 0, 0);

  x = (lcdGetWidth() - logo.w) / 2;
  y = (lcdGetHeight() - logo.h) / 2 - 16;

  for (int i=0; i<32; i++)
  {
    lcdClearBuffer(black);
    lcdDrawImage(&logo, x, y + ((31-i)*3));

    // lcdDrawRect(0, 0, LCD_WIDTH-0, LCD_HEIGHT-0, white);
    // lcdDrawRect(1, 1, LCD_WIDTH-2, LCD_HEIGHT-2, white);

    lcdUpdateDraw();
  }

  is_logo_on = true;
}
#else
void lcdLogoOn(void)
{
  image_t logo;
  int16_t x, y;
  
  logo = lcdCreateImage(&logo_img, 0, 0, 0, 0);

  x = (lcdGetWidth() - logo.w) / 2;
  y = (lcdGetHeight() - logo.h) / 2;

  lcdClearBuffer(black);
  lcdDrawImage(&logo, x, y - 16);

  lcdUpdateDraw();

  is_logo_on = true;
}
#endif

void lcdLogoDraw(int16_t x, int16_t y)
{
  image_t logo;
  
  logo = lcdCreateImage(&logo_img, 0, 0, 0, 0);

  if (x < 0)
    x = (lcdGetWidth() - logo.w) / 2;
  if (y < 0)
    y = (lcdGetHeight() - logo.h) / 2 - 16;

  lcdDrawImage(&logo, x, y);
}

void lcdLogoOff(void)
{
  lcdClearBuffer(black);
  lcdUpdateDraw();

  is_logo_on = false;
}

bool lcdLogoIsOn(void)
{
  return is_logo_on;
}
#endif

#ifdef _USE_HW_CLI
void cliLcd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("Driver   : ST7701\n");
    cliPrintf("Width    : %d\n", LCD_WIDTH);
    cliPrintf("Height   : %d\n", LCD_HEIGHT);
    cliPrintf("BKL      : %d%%\n", lcdGetBackLight());
   
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "logo") == true)
  {
    #if HW_LCD_LOGO == 1
    lcdLogoOn();
    #endif
    ret = true;
  }
 
   if (args->argc == 1 && args->isStr(0, "draw") == true)
  {
    
    uint32_t pre_time;
    uint32_t exe_time;

    for (int cnt=0; cnt<10; cnt++)
    {
      uint16_t color[3] = {red, green, blue};

      lcdClearBuffer(color[cnt%3]);

      pre_time = millis();
      lcdUpdateDraw();
      exe_time = millis()-pre_time;
      logPrintf("draw time : %d ms, %d fps\n", exe_time, 1000/exe_time);

      delay(500);
    }

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test") == true)
  {
    uint8_t cnt = 0;
    uint16_t x = 0;
    uint32_t pre_time_info;
    uint32_t fps = 0;
    uint32_t fps_time = 0;
    uint32_t draw_time = 0;
    button_event_t btn_event;


    buttonEventInit(&btn_event, 5);
    lcdSetFont(LCD_FONT_HAN);

    pre_time_info = 0;
    while(cliKeepLoop())
    {
      uint32_t pre_time;
      uint32_t exe_time;

      if (lcdDrawAvailable() == true)
      {
        pre_time = millis();
        lcdClearBuffer(black);

        lcdPrintfRect(0, 0, LCD_WIDTH, 32, green, 32, 
                      LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                      "[LCD 테스트]");

        if (millis()-pre_time_info >= 500)
        {
          pre_time_info = millis();
          fps = lcdGetFps();
          fps_time = lcdGetFpsTime();
          draw_time = lcdGetDrawTime();
        }
        lcdPrintf(0,16*1, white, "%2d fps", fps);
        lcdPrintf(0,16*2, white, "%2d ms" , fps_time);
        lcdPrintf(0,16*3, white, "%2d ms free" , fps_time - draw_time);
        lcdPrintfResize(LCD_WIDTH-30, 16, white, 24, "%02d", cnt%100);
        lcdPrintfResize(LCD_WIDTH-40, 40, white, 32, "%02d", cnt%100);
        
        cnt++;

        lcdDrawFillRect( 0, 70, 10, 10, red);
        lcdDrawFillRect(10, 70, 10, 10, green);
        lcdDrawFillRect(20, 70, 10, 10, blue);
      
        // lcdDrawFillRect(x, 90, 30*2, 30*2, red);
        // lcdDrawFillRect(lcdGetWidth()-x, 90+30*2, 30*2, 30*2, green);
        // lcdDrawFillRect(x + 30, 90+30*4, 30*2, 30*2, blue);

        lcdDrawFillRect(((cnt*3)%(LCD_WIDTH-100)), 100, 100, (LCD_HEIGHT-102), red);
        lcdDrawFillRect(((cnt*5)%(LCD_WIDTH-100)), 100, 100, (LCD_HEIGHT-102), green);
        lcdDrawFillRect(((cnt*6)%(LCD_WIDTH-100)), 100, 100, (LCD_HEIGHT-102), blue);

        x += 3;
        x %= lcdGetWidth();

        exe_time = millis()-pre_time;
        lcdPrintf(0, 120, red, "draw %d ms", exe_time);

        lcdRequestDraw();
      }
      delay(1);

      if (buttonEventGetPressed(&btn_event, _DEF_BUTTON1))
      {
        break;
      }      
    }
    buttonEventRemove(&btn_event);
    lcdUpdateDraw();

    lcdClear(black);
    lcdLogoOn();

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "touch") == true)
  {
    touch_info_t info;
    uint32_t cnt = 0;
    uint32_t fps = 0;
    uint32_t fps_time = 0;
    button_event_t btn_event;


    buttonEventInit(&btn_event, 5);
    lcdSetFont(LCD_FONT_HAN);
    touchClear();
    while(cliKeepLoop())
    {
      if (lcdDrawAvailable() == true)
      {
        lcdClearBuffer(black);

        lcdPrintfRect(0, 0, LCD_WIDTH, 32, green, 32, 
                      LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                      "[TOUCH 테스트]");

        if (cnt%30 == 0)
        {
          fps = lcdGetFps();
          fps_time = lcdGetFpsTime();
        }
        cnt++;
        lcdPrintf(5,16*1, white, "%d fps", fps);
        lcdPrintf(5,16*2, white, "%d ms " , fps_time);
        

        touchGetInfo(&info);

        for (int i=0; i<info.count; i++)
        {
          uint16_t color[5] = {red, green, blue, pink, brown};
          int16_t x;
          int16_t y;
          int16_t size;

          x = info.point[i].x;
          y = info.point[i].y;
          size = info.point[i].w * 3;
          lcdPrintf(x, y-80, white, "%d:%d", x, y);
          lcdDrawFillCircle(x, y, size, color[info.point[i].id]);
        }     
        lcdRequestDraw();
      }
      delay(1);

      if (buttonEventGetPressed(&btn_event, _DEF_BUTTON1))
      {
        break;
      }      
    }
    buttonEventRemove(&btn_event);
    lcdClear(black);
    lcdLogoOn();

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "pdm"))
  {
    bool clean_buffer = true;
    int32_t point_x;
    uint32_t point_i;
    uint32_t pdm_len;
    pcm_data_t pcm_buf_line[LCD_WIDTH/2] = {0, };
    uint32_t pcm_buf_i = 0;
    uint32_t pcm_buf_len = LCD_WIDTH/2;
    pcm_data_t *p_pcm_record_buf;
    uint32_t pcm_record_len;
    button_event_t btn_event;
    bool is_recorded = false;
    uint8_t i2s_ch = i2sGetEmptyChannel();
    uint32_t play_i = 0;
    uint32_t play_len = 0;
    bool is_play = false;

    pcm_record_len = pdmGetTimeToLengh(10*1000);
    p_pcm_record_buf = memMalloc(pcm_record_len * sizeof(pcm_data_t));
    if (p_pcm_record_buf == NULL)
    {
      cliPrintf("memMalloc() fail\n");
      return;
    }

    buttonEventInit(&btn_event, 5);

    i2sSetSampleRate(pdmGetSampleRate());
    pdmBegin();
    point_i = 0;
    while(cliKeepLoop())
    {
      touch_info_t info;

      if (touchGetInfo(&info) == true)
      {
        if (info.count >= 2)
        {
          break;
        }
      } 

      if (buttonGetPressed(_DEF_BUTTON1))
      {
        if (buttonGetPressedTime(_DEF_BUTTON1) > 1000)
        {
          if (pdmRecordIsBusy() == false && is_recorded == false)
          {
            pdmRecordStart(p_pcm_record_buf, pcm_record_len);
            cliPrintf("record start\n");
            is_recorded = true;
          }
        }
        else
        {
          is_recorded = false;
        }
      }
      if (buttonEventGetReleased(&btn_event, _DEF_BUTTON1))
      {
        if (pdmRecordIsBusy())
        {
          pdmRecordStop();
          cliPrintf("record stop\n");
          is_recorded = false;
        }
        else if (is_recorded == false && pdmRecordGetLength() > 0)
        {
          if (is_play)
          {
            is_play = false;
            is_recorded = false;
            cliPrintf("play stop\n");
          }
          else
          {
            is_play = true;
            play_i = 0;
            play_len = pdmRecordGetLength();
            cliPrintf("play start\n");
          }
        }
      }


      if (is_play)
      {
        uint32_t len;
        
        len = i2sAvailableForWrite(i2s_ch);
        len -= (len % 2);

        if (len > 0)
        {
          i2sWrite(i2s_ch, (int16_t *)&p_pcm_record_buf[play_i], len);
          play_i += (len/2);
        }

        if (play_i >= play_len)
        {
          is_play = false;
          cliPrintf("play stop\n");
        }
      }


      if (lcdDrawAvailable() == true)
      {
        if (clean_buffer == true)
        {
          clean_buffer = false;
          lcdClearBuffer(black);
          point_x = 0;

          lcdDrawFillRect(LCD_WIDTH/2 - 1, 0, 3, LCD_HEIGHT, blue);
          lcdPrintfRect(0, 0, LCD_WIDTH/2, 32, green, 32, 
                        LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                        "L-PDM");          
          lcdPrintfRect(LCD_WIDTH/2, 0, LCD_WIDTH/2, 32, green, 32, 
                        LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                        "R-PDM");          
        }

        pdm_len = pdmAvailable();
        if (pdm_len > 0)
        {
          pcm_data_t pcm_data;
          int16_t l_y;
          int16_t r_y;

          for (int i=0; i<pdm_len; i++)
          {
            pdmRead(&pcm_data, 1);  

            l_y = cmap(pcm_data.L, -32768, 32767, -50, 50);
            r_y = cmap(pcm_data.R, -32768, 32767, -50, 50);

            if (point_i%50 == 0)
            {
              pcm_buf_line[pcm_buf_i] = pcm_data;
              pcm_buf_i = (pcm_buf_i + 1) % pcm_buf_len;              
            }

            lcdDrawPixel(point_x            , LCD_HEIGHT-50 - l_y, red);
            lcdDrawPixel(point_x+LCD_WIDTH/2, LCD_HEIGHT-50 - r_y, red);
            
            point_i++;
            point_x++;
            if (point_x >= LCD_WIDTH/2)
            {
              uint32_t index;

              int16_t pre_x[2] = {0};
              int16_t pre_y[2] = {0};
              int16_t x[2];
              int16_t y[2];
              for (int i=0; i<LCD_WIDTH/2-1; i++)
              {
                index = (pcm_buf_i + i) % pcm_buf_len;
                l_y = cmap(pcm_buf_line[index].L, -32768, 32767, -100, 100);
                r_y = cmap(pcm_buf_line[index].R, -32768, 32767, -100, 100);
                
                x[0] = i;
                y[0] = LCD_HEIGHT-200 - l_y;
                x[1] = i+LCD_WIDTH/2;
                y[1] = LCD_HEIGHT-200 - r_y;

                if (i > 0)
                {
                  lcdDrawLine(pre_x[0], pre_y[0], x[0], y[0], green);
                  lcdDrawLine(pre_x[1], pre_y[1], x[1], y[1], green);
                }

                pre_x[0] = x[0];
                pre_x[1] = x[1];
                pre_y[0] = y[0];
                pre_y[1] = y[1];                
              }
              clean_buffer = true;


              if (pdmRecordIsBusy())
              {
                lcdDrawFillRect(240/2, 64, 240, 64, red);
                lcdPrintfRect(0, 64, LCD_WIDTH, 32, white, 32, 
                              LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                              "녹음중");                 
                lcdPrintfRect(0, 64+32, LCD_WIDTH, 32, white, 32, 
                              LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                              " %d %%", pdmRecordGetLength() * 100 / pcm_record_len);                 
              }
              if (is_play)
              {
                lcdDrawFillRect(240/2, 64, 240, 64, white);
                lcdPrintfRect(0, 64, LCD_WIDTH, 32, black, 32, 
                              LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                              "재생중");                 
                lcdPrintfRect(0, 64+32, LCD_WIDTH, 32, black, 32, 
                              LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                              " %d %%", play_i * 100 / play_len);                 
              }

              int32_t pdm_dir = pdmDirGetAngle();              
              lcdPrintf(0, 32, white, "DIR : %d", pdm_dir);

              pdm_dir = cmap(pdm_dir, -90, 90, -240, 240);
              if (pdm_dir > 0)
                lcdDrawFillRect(LCD_WIDTH/2, 128, abs(pdm_dir), 32, green);
              else
                lcdDrawFillRect(LCD_WIDTH/2 + pdm_dir, 128, abs(pdm_dir), 32, green);

              lcdRequestDraw();
              break;
            }
          }
        }
      }
      delay(1);
    }
    pdmEnd();

    buttonEventRemove(&btn_event);
    memFree(p_pcm_record_buf);
    lcdClear(black);
    lcdLogoOn();

    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "bl") == true)
  {
    uint8_t bl_value;

    bl_value = args->getData(1);

    lcdSetBackLight(bl_value);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rgb") == true)
  {
    uint16_t line;
    uint16_t line_h = 2;

    lcdClearBuffer(black);
    lcdUpdateDraw();

    line = 0;
    for (int i=0; i<(255>>2); i++)
    {
      uint16_t color;

      color = RGB2COLOR(i<<2, 0, 0);
      lcdDrawFillRect(0, line, LCD_WIDTH, line_h, color); 
      line += line_h;
    }
    for (int i=0; i<(255>>2); i++)
    {
      uint16_t color;

      color = RGB2COLOR(0, i<<2, 0);
      lcdDrawFillRect(0, line, LCD_WIDTH, line_h, color); 
      line += line_h;
    }
    for (int i=0; i<(255>>2); i++)
    {
      uint16_t color;

      color = RGB2COLOR(0, 0, i<<2);
      lcdDrawFillRect(0, line, LCD_WIDTH, line_h, color); 
      line += line_h;
    }


    lcdUpdateDraw();
    
    while(cliKeepLoop())
    {

    }

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "gray") == true)
  {
    uint16_t line;
    uint16_t line_h = 2;

    lcdClearBuffer(black);
    lcdUpdateDraw();

    line = 0;
    for (int i=0; i<(255>>0); i++)
    {
      uint16_t color;

      color = RGB2COLOR(i<<0, i<<0, i<<0);
      lcdDrawFillRect(0, line, LCD_WIDTH, line_h, color); 
      line += line_h;
    }

    lcdUpdateDraw();
    
    while(cliKeepLoop())
    {

    }

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("lcd info\n");
    cliPrintf("lcd draw\n");    
    cliPrintf("lcd logo\n");
    cliPrintf("lcd test\n");
    cliPrintf("lcd touch\n");
    cliPrintf("lcd pdm\n");
    cliPrintf("lcd rgb\n");
    cliPrintf("lcd gray\n");
    cliPrintf("lcd bl 0~100\n");
  }
}
#endif



#endif
