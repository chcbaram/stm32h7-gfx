#include "ap.h"



static bool apModuleInit(void);
static void apUpdate(void const *arg);
static void updateLed(void);
static void updateButton(void);


static button_event_t btn_evt;




void apInit(void)
{
  moduleInit();
  threadBegin();
}

void apMain(void)
{
  while(1)
  {
    moduleUpdate();
    delay(1);
  }
}

bool apModuleInit(void)
{
  buttonEventInit(&btn_evt, 5);
  return true;
}

void apUpdate(void const *arg)
{
  UNUSED(arg);

  eventUpdate();
  updateLed();
  updateButton();
  sdUpdate();
}

void updateLed(void)
{
  static uint32_t pre_time = 0;


  if (millis() - pre_time >= 500)
  {
    pre_time = millis();
    ledToggle(_DEF_LED1);
  }
}

void updateButton(void)
{
  static uint8_t mode = 0;


  if (buttonEventGetPressed(&btn_evt, _DEF_BUTTON1) && cliIsBusy() == false)
  {
    if (mode == 0)
    {
      cliRunStr("lcd pdm");
      buttonEventClear(&btn_evt);
    }
    if (mode == 1)
    {
      cliRunStr("lcd touch");
      buttonEventClear(&btn_evt);
    }
    if (mode == 2)
    {
      cliRunStr("lcd test");
      buttonEventClear(&btn_evt);
    }

    mode = (mode + 1) % 3;
  }
}


MODULE_DEF(ap){
  .name     = "ap",
  .priority = MODULE_PRI_LOW,
  .init     = apModuleInit,
  .update   = apUpdate,
};
