#include "main.h"



static void mainThread(void const *arg);


int main(void)
{
  bspInit();


  osThreadDef(mainThread, mainThread, osPriorityNormal, 0, 512);
  if (osThreadCreate(osThread(mainThread), NULL) == NULL)
  {
    ledInit();

    while(1)
    {
      ledOn(_DEF_LED1);
      delay(50);
      ledOff(_DEF_LED1);
      delay(50);
      ledOn(_DEF_LED1);
      delay(500);
      ledOff(_DEF_LED1);
      delay(500);
    }
  }

  osKernelStart();  
  return 0;
}

void mainThread(void const *arg)
{
  UNUSED(arg);

  hwInit();
  apInit();
  apMain();
}