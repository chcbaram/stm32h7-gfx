#include "cli.h"
#include "thread.h"


namespace cli
{
bool init(void);
void main(void const *arg);
}



__attribute__((section(".thread"))) 
static volatile thread_t thread_obj = 
  {
    .name = "cli",
    .init = cli::init,
    .main = cli::main,
    .priority = osPriorityNormal,
    .stack_size = 8*1024
  };


bool cli::init(void)
{
  cliOpen(_DEF_UART1, 115200);
  logBoot(false);
  return true;
}

void cli::main(void const *arg)
{
  uint8_t cli_ch;


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

