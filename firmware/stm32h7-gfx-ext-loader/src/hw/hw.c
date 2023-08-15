#include "hw.h"
#include "lcd/st7701.h"








bool hwInit(void)
{
  bool ret = true;

  bspInit();

  ledInit();

  ret = qspiInit(); 
  
  return ret;
}