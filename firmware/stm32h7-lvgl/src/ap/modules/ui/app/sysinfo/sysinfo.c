#include "sysinfo.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

static bool sysinfoEnter(lv_obj_t *scr);
static void sysinfoUpdate(void);
static void backEventCb(lv_event_t *e);


static lv_obj_t *label_fps = NULL;
static lv_obj_t *label_mem = NULL;
static uint32_t  frame_cnt = 0;
static uint32_t  pre_time = 0;




bool sysinfoInit(void)
{
  return true;
}

bool sysinfoEnter(lv_obj_t *scr)
{
  lv_obj_t *label;
  lv_obj_t *card;
  lv_obj_t *spinner;


  label = uiCreateLabel(scr, _DEF_BOARD_NAME, uiStyleTextTitle());
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 24);

  label = uiCreateLabel(scr, _DEF_FIRMWATRE_VERSION, uiStyleTextDim());
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 84);

  spinner = lv_spinner_create(scr);
  lv_obj_set_size(spinner, 100, 100);
  lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 124);
  lv_spinner_set_anim_params(spinner, 1000, 60);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(UI_COLOR_SURFACE_ALT), LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);

  /* 스피너 아래(224) 부터 뒤로가기 버튼 위(392) 사이에 놓는다. */
  card = uiCreateCard(scr, LCD_WIDTH - UI_MARGIN*2, 160);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 230);

  label = uiCreateLabel(card, "LVGL", uiStyleTextDim());
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  label = uiCreateLabel(card, "", uiStyleTextBody());
  lv_label_set_text_fmt(label, "v%d.%d.%d",
                        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_obj_align(label, LV_ALIGN_TOP_RIGHT, 0, -2);

  label = uiCreateLabel(card, "frame", uiStyleTextDim());
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  label_fps = uiCreateLabel(card, "-", uiStyleTextBody());
  lv_obj_align(label_fps, LV_ALIGN_RIGHT_MID, 0, -2);

  label = uiCreateLabel(card, "lvgl mem", uiStyleTextDim());
  lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  label_mem = uiCreateLabel(card, "-", uiStyleTextBody());
  lv_obj_align(label_mem, LV_ALIGN_BOTTOM_RIGHT, 0, -2);


  uiCreateBackButton(scr, backEventCb);

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
    lv_label_set_text_fmt(label_mem, "%d %%", (int)mon.used_pct);
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
