#include "info.h"


#ifdef _USE_HW_CLI

static void cliInfo(cli_args_t *args);



bool infoInit(void)
{
  cliAdd("info", cliInfo);
  return true;
}

void cliInfo(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "cpu"))
  {
    while(cliKeepLoop())
    {
      cliPrintf("cpu usage : %d %%\r", osGetCPUUsage());
      delay(100);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("info cpu\n");
  }
}

MODULE_DEF(info){
  .name     = "info",
  .priority = MODULE_PRI_NORMAL,
  .init     = infoInit,
};

#endif
