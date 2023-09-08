#include "thread.h"


#if _USE_AP_VIDEO == 1





static void cliCmd(cli_args_t *args);
static bool videoInit(void);
static void videoMain(void const *arg);



__attribute__((section(".thread"))) 
static volatile thread_t thread_obj = 
  {
    .name = "video",
    .init = videoInit,
    .main = videoMain,
    .priority = osPriorityNormal,
    .stack_size = 8*1024
  };


static bool is_enable = true;






bool videoInit(void)
{
  cliAdd("video", cliCmd);
  return true;
}

void videoMain(void const *arg)
{
  while(1)
  {
    delay(1);
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

  if (args->argc == 2 && args->isStr(0, "play"))
  {
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("video info\n");
    cliPrintf("video play avi\n");
  }
}


#endif