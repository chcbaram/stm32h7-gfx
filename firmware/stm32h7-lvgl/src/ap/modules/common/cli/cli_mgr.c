#include "cli_mgr.h"


#ifdef _USE_HW_CLI

static void cliMgrThread(void const *arg);



bool cliMgrInit(void)
{
  cliOpen(HW_UART_CH_SWD, 115200);
  logBoot(false);

  return threadCreate("cli", cliMgrThread, NULL, osPriorityNormal, 8*1024);
}

void cliMgrThread(void const *arg)
{
  uint8_t cli_ch;

  UNUSED(arg);

  while(1)
  {
    if (usbIsOpen() && usbGetType() == USB_CON_CLI)
    {
      cli_ch = HW_UART_CH_USB;
    }
    else
    {
      cli_ch = HW_UART_CH_SWD;
    }
    if (cli_ch != cliGetPort())
    {
      cliOpen(cli_ch, 0);
    }
    cliMain();
    delay(1);
  }
}

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_HIGH,
  .init     = cliMgrInit,
};

#endif
