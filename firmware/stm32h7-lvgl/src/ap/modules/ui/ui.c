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

  /* SPI Flash 의 한글 폰트를 fallback 으로 로드한다. (없으면 영문만 표시)

     크기별로 따로 둔다. fallback 은 크기를 안 바꾸므로 28px 하나로 20px 캡션까지
     덮으면 한글만 크게 나와 줄을 넘친다. */
  {
    lv_font_t *kr    = lv_binfont_create("F:/font/kr_28.bin");     /* 28px */
    lv_font_t *kr_20 = lv_binfont_create("F:/font/kr_20.bin");  /* 20px */

    if (kr_20 != NULL)
      logPrintf("[OK] kr_20 font loaded\n");
    else
      logPrintf("[  ] kr_20 font not found (F:/font/kr_20.bin)\n");

    if (kr != NULL)
      logPrintf("[OK] kr_28 font loaded\n");
    else
      logPrintf("[  ] kr_28 font not found (F:/font/kr_28.bin)\n");
    uiThemeSetKrFont(kr, kr, (kr_20 != NULL) ? kr_20 : kr);
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
