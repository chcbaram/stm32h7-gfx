#include "lv_port_disp.h"
#include "lcd.h"
#include "ltdc.h"


#define BYTE_PER_PIXEL  (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define FLUSH_TIMEOUT   50


static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);


static lv_display_t   *disp = NULL;
static volatile bool   disp_flush_enabled = true;




void lv_port_disp_init(void)
{
  uint16_t *b0 = ltdcGetPhysBuffer(0);
  uint16_t *b1 = ltdcGetPhysBuffer(1);
  uint16_t *shown = ltdcGetDisplayedBuffer();
  uint16_t *buf_draw;
  uint16_t *buf_front;


  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, disp_flush);

  /* LTDC 하드웨어 더블버퍼 두 장을 그대로 LVGL 의 더블버퍼로 넘긴다.
   *
   * DIRECT 모드 : 변경된 영역만 다시 그리므로 효율적이다. LVGL 이 두 버퍼를
   * 스스로 번갈아 쓰고, flush 때 방금 그린 버퍼를 LTDC 표시 주소로 지정한다.
   * (표시 버퍼를 LVGL 이 단독으로 결정하므로 배경/오버레이가 어긋나지 않는다.)
   *
   * LVGL 은 첫 버퍼(buf1)부터 그리므로, 지금 표시중이 아닌 버퍼를 buf1 로 준다.
   */
  buf_front = shown;
  buf_draw  = (shown == b0) ? b1 : b0;

  lv_display_set_buffers(disp,
                         buf_draw,
                         buf_front,
                         LCD_WIDTH * LCD_HEIGHT * BYTE_PER_PIXEL,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
}

void disp_enable_update(void)
{
  disp_flush_enabled = true;
}

void disp_disable_update(void)
{
  disp_flush_enabled = false;
}

/* DIRECT 모드라 px_map 은 LVGL 이 방금 그린 프레임버퍼(현재 back)다.
 * 마지막 area 일 때만 그 버퍼를 LTDC 표시로 지정하고, vblank reload 가
 * 끝날 때까지 기다린다. reload 가 끝나야 이전 버퍼가 자유로워져 LVGL 이
 * 다음 프레임을 안전하게 그릴 수 있다.
 */
void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
  LV_UNUSED(area);

  if (disp_flush_enabled == true && lv_display_flush_is_last(disp_drv) == true)
  {
    uint32_t pre_time;

    ltdcLvglFlush(px_map);

    pre_time = millis();
    while (ltdcLvglIsReloadDone() == false)
    {
      if (millis() - pre_time >= FLUSH_TIMEOUT)
        break;
      osThreadYield();
    }
  }

  lv_display_flush_ready(disp_drv);
}
