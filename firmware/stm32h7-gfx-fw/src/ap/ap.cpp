#include "ap.h"
#include "thread.h"




void apInit(void)
{  
  thread::init();
}

void apMain(void)
{
  uint32_t pre_time;
  button_event_t btn_evt;
  uint8_t mode = 0;



  buttonEventInit(&btn_evt, 5);

  pre_time = millis();
  while(1)
  {
    if (millis()-pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }    
    sdUpdate();
    delay(10);

    if (buttonEventGetPressed(&btn_evt, _DEF_BUTTON1) && cliIsBusy() == false)
    {
      if (mode == 0)
      {
        cliRunStr("touchgfx test");
        buttonEventClear(&btn_evt);
      }      
      if (mode == 1)
      {
        cliRunStr("lcd pdm");
        buttonEventClear(&btn_evt);
      }
      if (mode == 2)
      {
        cliRunStr("lcd touch");
        buttonEventClear(&btn_evt);
      }
      if (mode == 3)
      {
        cliRunStr("lcd test");
        buttonEventClear(&btn_evt);
      }

      mode = (mode + 1) % 4;
    }
  }
}

