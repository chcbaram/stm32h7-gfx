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
