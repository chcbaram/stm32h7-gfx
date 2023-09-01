#include "thread.h"


#if _USE_AP_TOUCHGFX == 1

#include <TouchGFXGeneratedHAL.hpp>
#include <touchgfx/hal/OSWrappers.hpp>

using namespace touchgfx;


extern "C"
{
void touchgfx_init(void);
void touchgfx_components_init(void);
void touchgfx_taskEntry(void);
}



static void cliCmd(cli_args_t *args);
static bool touchgfxInit(void);
static void touchgfxMain(void const *arg);
static void touchgfxVsync(uint8_t mode);


__attribute__((section(".thread"))) 
static volatile thread_t thread_obj = 
  {
    .name = "touchgfx",
    .init = touchgfxInit,
    .main = touchgfxMain,
    .priority = osPriorityNormal,
    .stack_size = 8*1024
  };


static bool is_enable = false;






bool touchgfxInit(void)
{
  touchgfx_components_init();
  touchgfx_init();

  ltdcSetVsyncFunc(touchgfxVsync);

  cliAdd("touchgfx", cliCmd);
  return true;
}

void touchgfxMain(void const *arg)
{
  while(1)
  {
    touchgfx_taskEntry();
  }
}

void touchgfxVsync(uint8_t mode)
{
  if (is_enable == false)
    return;

  if (HAL::getInstance() == NULL)
    return;

  if (mode == 0)
  {
    HAL::getInstance()->vSync();
    OSWrappers::signalVSync();
    HAL::getInstance()->swapFrameBuffers();
  }
  else
  {
    HAL::getInstance()->frontPorchEntered();
  }
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_enable : %s\n", is_enable ? "True":"False");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    button_event_t btn_evt;


    HAL::getInstance()->flushFrameBuffer();
    delay(50);
    HAL::getInstance()->flushFrameBuffer();
    delay(50);    
    is_enable = true;
   
    buttonEventInit(&btn_evt, 5);
    while(cliKeepLoop())
    {
      delay(10);
      if (buttonEventGetPressed(&btn_evt, _DEF_BUTTON1))
      {
        break;
      }
    }
    
    is_enable = false;
    delay(100);

    lcdClear(black);
    lcdLogoOn();

    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("touchgfx info\n");
    cliPrintf("touchgfx test\n");
  }
}


#endif