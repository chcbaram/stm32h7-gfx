#include "info.h"
#include "thread.h"


namespace info
{
bool init(void);
void main(void const *arg);
}

static void cliInfo(cli_args_t *args);


__attribute__((section(".thread"))) 
static volatile thread_t thread_obj = 
  {
    .name = "info",
    .init = info::init,
    .main = info::main,
    .priority = osPriorityNormal,
    .stack_size = 2*1024
  };


bool info::init(void)
{
  cliAdd("info", cliInfo);
  return true;
}

void info::main(void const *arg)
{

  while(1)
  {
    delay(100);
  }
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
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "thread"))
  {
    extern uint32_t _sthread;
    extern uint32_t _ethread;

    int thread_count;
    thread_t *p_thread;

    cliPrintf("cpu usage : %d %%\n", osGetCPUUsage());

    thread_count = ((int)&_ethread - (int)&_sthread)/sizeof(thread_t);
    for (int i=0; i<thread_count; i++)
    {
      p_thread = (thread_t *)&_sthread;
      cliPrintf("%-16s, stack : %4d, prio : %d\n",
                p_thread->name,
                p_thread->stack_size,
                p_thread->priority
                );
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "task"))
  {
    const size_t bytes_per_task = 40;
    char *list_buf;

    list_buf = (char *)pvPortMalloc(uxTaskGetNumberOfTasks() * bytes_per_task);

    vTaskList(list_buf);
    cliPrintf("Task Name\tState\tPrio\tStack\tNum#\n");
    cliWrite((uint8_t *)list_buf, strlen(list_buf));
    vPortFree(list_buf);

    cliPrintf("Free Heap : %d bytes\n", xPortGetFreeHeapSize());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("info cpu\n");
    cliPrintf("info thread\n");
    cliPrintf("info task\n");
  }
}
