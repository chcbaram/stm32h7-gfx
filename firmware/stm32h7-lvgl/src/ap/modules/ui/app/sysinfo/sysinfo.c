#include "sysinfo.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

static bool sysinfoEnter(lv_obj_t *scr);
static void sysinfoUpdate(void);
static void backEventCb(lv_event_t *e);


static lv_obj_t *label_fps  = NULL;
static lv_obj_t *label_mem  = NULL;
static uint32_t  frame_cnt  = 0;
static uint32_t  pre_time   = 0;




bool sysinfoInit(void)
{
  return true;
}

bool sysinfoEnter(lv_obj_t *scr)
{
  lv_obj_t *label;
  lv_obj_t *spinner;
  lv_obj_t *btn_back;


  lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);

  label = lv_label_create(scr);
  lv_label_set_text_fmt(label, "LVGL v%d.%d.%d",
                        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF0F0F0), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 40);

  label = lv_label_create(scr);
  lv_label_set_text(label, _DEF_BOARD_NAME);
  lv_obj_set_style_text_color(label, lv_color_hex(0x8090A0), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 70);

  spinner = lv_spinner_create(scr);
  lv_obj_set_size(spinner, 110, 110);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
  lv_spinner_set_anim_params(spinner, 1000, 60);

  label_fps = lv_label_create(scr);
  lv_label_set_text(label_fps, "0 fps");
  lv_obj_set_style_text_color(label_fps, lv_color_hex(0x50C878), LV_PART_MAIN);
  lv_obj_align(label_fps, LV_ALIGN_CENTER, 0, 90);

  label_mem = lv_label_create(scr);
  lv_label_set_text(label_mem, "-");
  lv_obj_set_style_text_color(label_mem, lv_color_hex(0x8090A0), LV_PART_MAIN);
  lv_obj_align(label_mem, LV_ALIGN_CENTER, 0, 120);

  btn_back = lv_button_create(scr);
  lv_obj_set_size(btn_back, 60, 44);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x3A4048), LV_PART_MAIN);
  lv_obj_add_event_cb(btn_back, backEventCb, LV_EVENT_CLICKED, NULL);

  label = lv_label_create(btn_back);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_center(label);

  frame_cnt = 0;
  pre_time  = millis();
  return true;
}

void sysinfoUpdate(void)
{
  frame_cnt++;

  if (millis() - pre_time >= 1000)
  {
    lv_mem_monitor_t mon;

    pre_time = millis();

    lv_label_set_text_fmt(label_fps, "%d fps", (int)frame_cnt);
    frame_cnt = 0;

    lv_mem_monitor(&mon);
    lv_label_set_text_fmt(label_mem, "mem %d %%", (int)mon.used_pct);
  }
}

void backEventCb(lv_event_t *e)
{
  LV_UNUSED(e);
  launcherExitApp();
}


APP_DEF(sysinfo){
  .name   = "System Info",
  .order  = 10,
  .init   = sysinfoInit,
  .enter  = sysinfoEnter,
  .update = sysinfoUpdate,
};

#endif
