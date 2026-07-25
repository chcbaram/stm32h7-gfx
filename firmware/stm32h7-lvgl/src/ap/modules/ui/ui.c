#include "ui.h"
#include "launcher.h"
#include "ui_shade.h"


#ifdef _USE_HW_LVGL

#define UI_THREAD_STACK   (8*1024)


static void uiThread(void const *arg);




bool uiInit(void)
{
  bool ret;


#ifdef _USE_HW_LCD
  lcdLogoOff();
#endif

  lvglInit();

  /* SPI Flash 의 한글 폰트를 fallback 으로 로드한다. (없으면 영문만 표시) */
  {
    lv_font_t *kr = lv_binfont_create("F:/font/kr.bin");
    if (kr != NULL)
      logPrintf("[OK] kr font loaded\n");
    else
      logPrintf("[  ] kr font not found (S:/font/kr.bin)\n");
    uiThemeSetKrFont(kr);
  }

  uiThemeInit();
  launcherInit();
  ui_shade_init();

  ret = threadCreate("ui", uiThread, NULL, osPriorityNormal, UI_THREAD_STACK);

  logPrintf("[%s] uiInit()\n", ret ? "OK":"NG");
  return ret;
}

void uiThread(void const *arg)
{
  UNUSED(arg);

  while(1)
  {
    launcherUpdate();
    lvglUpdate();

    delay(5);
  }
}


MODULE_DEF(ui){
  .name     = "ui",
  .priority = MODULE_PRI_NORMAL,
  .init     = uiInit,
};

#endif
