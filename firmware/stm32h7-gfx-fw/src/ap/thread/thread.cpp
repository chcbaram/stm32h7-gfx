#include "thread.h"





typedef struct
{
  int32_t count;

  thread_t *p_thread;
  bool is_begin;

} thread_info_t;

namespace thread
{
static bool begin(void);
}

static thread_info_t info;
extern uint32_t _sthread;
extern uint32_t _ethread;





bool thread::init(void)
{
  info.is_begin = false;

  info.count = ((int)&_ethread - (int)&_sthread)/sizeof(thread_t);
  info.p_thread = (thread_t *)&_sthread;

  logPrintf("[  ] thread::init()\n");
  logPrintf("     count : %d\n", info.count);

  thread::begin();

  return true;
}

bool thread::begin(void)
{
  bool ret = true;

  logPrintf("[  ] thread::begin()\n");

  for (int i=0; i<info.count; i++)
  {
    if (info.p_thread[i].init != NULL)
    {
      ret &= info.p_thread[i].init();
    }

    if (info.p_thread[i].main != NULL)
    {
      info.p_thread[i].thread_def.name      = (char *)info.p_thread[i].name;
      info.p_thread[i].thread_def.pthread   = info.p_thread[i].main;
      info.p_thread[i].thread_def.instances = 0;
      info.p_thread[i].thread_def.stacksize = info.p_thread[i].stack_size/4;
      info.p_thread[i].thread_def.tpriority = info.p_thread[i].priority;
      info.p_thread[i].thread_id = osThreadCreate(&info.p_thread[i].thread_def, NULL);
      if (info.p_thread[i].thread_id != NULL)
        logPrintf("     %s OK\n", info.p_thread[i].name);
      else
        logPrintf("     %s Fail\n", info.p_thread[i].name);
    }
  }
  info.is_begin = ret;

  cliPrintf("     Free Heap : %d bytes\n", xPortGetFreeHeapSize());

  return ret;
}
