#include "lv_port_disp.h"
#include "lcd.h"
#include "ltdc.h"


#define BYTE_PER_PIXEL  (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define FLUSH_TIMEOUT   50


static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void disp_flush_wait(lv_display_t *disp);


static lv_display_t   *disp = NULL;
static volatile bool   disp_flush_enabled = true;




void lv_port_disp_init(void)
{
  uint16_t *p_buf_back;
  uint16_t *p_buf_front;


  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, disp_flush);
  lv_display_set_flush_wait_cb(disp, disp_flush_wait);

  /* LTDC 하드웨어 더블버퍼를 그대로 LVGL 의 더블버퍼로 사용한다.
   *
   * LTDC 는 vsync 마다 front/back 을 교체하고 LVGL 은 flush 마다 buf1/buf2 를
   * 교체하므로, 첫 버퍼를 현재 back(=그릴 수 있는 쪽)으로 넘겨주면 두 교체가
   * 같은 위상으로 맞물린다.
   */
  ltdcSetDoubleBuffer(true);

  p_buf_back  = ltdcGetFrameBuffer();         /* 지금 그릴 수 있는 back  */
  p_buf_front = ltdcGetCurrentFrameBuffer();  /* 지금 표시중인 front     */

  lv_display_set_buffers(disp,
                         p_buf_back,
                         p_buf_front,
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

/* DIRECT 모드라 px_map 은 이미 프레임버퍼 자체다.
 * 마지막 area 일 때만 LTDC 에 교체를 요청한다.
 */
void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
  LV_UNUSED(area);
  LV_UNUSED(px_map);

  if (disp_flush_enabled == true && lv_display_flush_is_last(disp_drv) == true)
  {
    ltdcRequestDraw();
  }

  lv_display_flush_ready(disp_drv);
}

/* LTDC 가 vsync 에서 실제로 버퍼를 교체할 때까지 기다린다.
 * 교체가 끝나야 LVGL 이 다음 버퍼에 안전하게 그릴 수 있다.
 */
void disp_flush_wait(lv_display_t *disp_drv)
{
  uint32_t pre_time = millis();

  LV_UNUSED(disp_drv);

  while (ltdcDrawAvailable() == false)
  {
    if (millis() - pre_time >= FLUSH_TIMEOUT)
      break;

    osThreadYield();
  }
}
