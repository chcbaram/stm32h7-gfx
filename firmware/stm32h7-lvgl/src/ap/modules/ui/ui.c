#include "ui.h"


#ifdef _USE_HW_LVGL

#define UI_THREAD_STACK   (8*1024)


static void uiThread(void const *arg);
static void uiCreateScreen(void);
static void uiBtnEventCb(lv_event_t *e);
#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif


static lv_obj_t *label_btn = NULL;
static lv_obj_t *label_fps = NULL;
static uint32_t  btn_cnt = 0;
static volatile uint32_t fps = 0;




bool uiInit(void)
{
  bool ret;


#ifdef _USE_HW_LCD
  lcdLogoOff();
#endif

  lvglInit();
  uiCreateScreen();

  ret = threadCreate("ui", uiThread, NULL, osPriorityNormal, UI_THREAD_STACK);

#ifdef _USE_HW_CLI
  cliAdd("ui", cliCmd);
#endif

  logPrintf("[%s] uiInit()\n", ret ? "OK":"NG");
  return ret;
}

void uiThread(void const *arg)
{
  uint32_t pre_time;
  uint32_t frame_cnt = 0;

  UNUSED(arg);

  pre_time = millis();

  while(1)
  {
    lvglUpdate();
    frame_cnt++;

    if (millis() - pre_time >= 1000)
    {
      pre_time = millis();
      fps = frame_cnt;
      frame_cnt = 0;

      if (label_fps != NULL)
      {
        lv_label_set_text_fmt(label_fps, "%d fps", (int)fps);
      }
    }

    delay(5);
  }
}

void uiCreateScreen(void)
{
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *label;
  lv_obj_t *btn;
  lv_obj_t *spinner;


  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), LV_PART_MAIN);

  /* 타이틀 */
  label = lv_label_create(scr);
  lv_label_set_text_fmt(label, "LVGL v%d.%d.%d",
                        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF0F0F0), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 30);

  /* 보드 이름 */
  label = lv_label_create(scr);
  lv_label_set_text(label, _DEF_BOARD_NAME);
  lv_obj_set_style_text_color(label, lv_color_hex(0x8090A0), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 60);

  /* 갱신이 살아있는지 보기 위한 스피너 */
  spinner = lv_spinner_create(scr);
  lv_obj_set_size(spinner, 100, 100);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -40);
  lv_spinner_set_anim_params(spinner, 1000, 60);

  /* 터치 확인용 버튼 */
  btn = lv_button_create(scr);
  lv_obj_set_size(btn, 200, 70);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
  lv_obj_add_event_cb(btn, uiBtnEventCb, LV_EVENT_CLICKED, NULL);

  label_btn = lv_label_create(btn);
  lv_label_set_text(label_btn, "Touch : 0");
  lv_obj_center(label_btn);

  /* 프레임 레이트 */
  label_fps = lv_label_create(scr);
  lv_label_set_text(label_fps, "0 fps");
  lv_obj_set_style_text_color(label_fps, lv_color_hex(0x50C878), LV_PART_MAIN);
  lv_obj_align(label_fps, LV_ALIGN_BOTTOM_MID, 0, -30);
}

void uiBtnEventCb(lv_event_t *e)
{
  LV_UNUSED(e);

  btn_cnt++;
  lv_label_set_text_fmt(label_btn, "Touch : %d", (int)btn_cnt);
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("fps       : %d\n", (int)fps);
    cliPrintf("btn_cnt   : %d\n", (int)btn_cnt);
    cliPrintf("is_enable : %s\n", lvglGetEnable() ? "True":"False");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ui info\n");
  }
}
#endif


MODULE_DEF(ui){
  .name     = "ui",
  .priority = MODULE_PRI_NORMAL,
  .init     = uiInit,
};

#endif
