#include "lv_port_indev.h"
#include "lcd.h"
#include "touch.h"


static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);


static lv_indev_t *indev_touchpad = NULL;




void lv_port_indev_init(void)
{
  indev_touchpad = lv_indev_create();

  lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

lv_indev_t *lv_port_indev_get_touch(void)
{
  return indev_touchpad;
}

void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  static int16_t last_x = 0;
  static int16_t last_y = 0;
  touch_info_t info;

  LV_UNUSED(indev);

  if (touchGetInfo(&info) == true && info.count > 0)
  {
    last_x = info.point[0].x;
    last_y = info.point[0].y;

    if (last_x < 0) last_x = 0;
    if (last_y < 0) last_y = 0;
    if (last_x >= LCD_WIDTH)  last_x = LCD_WIDTH  - 1;
    if (last_y >= LCD_HEIGHT) last_y = LCD_HEIGHT - 1;

    data->state = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }

  data->point.x = last_x;
  data->point.y = last_y;
}
