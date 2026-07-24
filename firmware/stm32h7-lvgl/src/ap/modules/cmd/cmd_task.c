#include "cmd_task.h"


#ifdef _USE_HW_CMD
#include "driver/drv_uart.h"
#include "process/cmd_file.h"


#define CMD_DRIVER_MAX_CH   1
#define CMD_THREAD_STACK    (4*1024)


static void cmdThread(void const *arg);
#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif

static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];

static uint32_t rx_packet_cnt = 0;




bool cmdTaskInit(void)
{
  bool ret;


  /* USB CDC 채널.
   *
   * CLI 는 호스트가 115200 으로 열었을 때만 USB 를 잡는다.
   * 호스트가 그 외 보율로 열면 CLI 는 SWD UART 로 물러나므로
   * 이 채널을 cmd 가 단독으로 쓰게 된다.
   */
  drvUartInit(&cmd_driver[0], HW_UART_CH_USB, 115200);
  cmdInit(&cmd[0], &cmd_driver[0]);
  cmdOpen(&cmd[0]);

  cmdFileInit();

  ret = threadCreate("cmd", cmdThread, NULL, osPriorityNormal, CMD_THREAD_STACK);

#ifdef _USE_HW_CLI
  cliAdd("cmd", cliCmd);
#endif

  logPrintf("[%s] cmdTaskInit()\n", ret ? "OK":"NG");
  return ret;
}

bool cmdTaskUpdate(void)
{
  bool rx_ret = false;

  /* 호스트가 115200 으로 열면 그 채널은 CLI 것이다.
   * 여기서 같이 읽으면 서로 바이트를 빼앗아 양쪽 다 깨진다.
   */
  if (usbIsOpen() == true && usbGetType() == USB_CON_CLI)
  {
    return false;
  }

  for (int i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    if (cmd[i].is_init != true)
      continue;

    if (cmdReceivePacket(&cmd[i]) == true)
    {
      bool ret = true;

      ret &= cmdFileProcess(&cmd[i]);

      if (ret != true)
      {
        cmdSendResp(&cmd[i], cmd[i].packet.cmd, ERR_CMD_NO_CMD, NULL, 0);
      }

      rx_packet_cnt++;
      rx_ret = true;
    }
  }

  return rx_ret;
}

void cmdThread(void const *arg)
{
  UNUSED(arg);

  while(1)
  {
    if (cmdTaskUpdate() != true)
    {
      delay(1);
    }
  }
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cmd_file_info_t info;

    cmdFileGetInfo(&info);

    cliPrintf("ch        : USB CDC\n");
    cliPrintf("rx packet : %d\n", (int)rx_packet_cnt);
    cliPrintf("max data  : %d\n", CMD_MAX_DATA_LENGTH);
    cliPrintf("busy      : %s\n", cmdFileIsBusy() ? "True":"False");
    if (info.is_begin == true)
    {
      cliPrintf("file      : %s\n", info.name);
      cliPrintf("recv      : %d / %d\n", (int)info.recv_size, (int)info.size);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("cmd info\n");
  }
}
#endif


MODULE_DEF(cmd){
  .name     = "cmd",
  .priority = MODULE_PRI_NORMAL,
  .init     = cmdTaskInit,
};

#endif
