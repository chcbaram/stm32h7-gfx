#ifndef THREAD_H_
#define THREAD_H_


#include "ap_def.h"



typedef struct thread_t_
{
  const char    *name;
  bool         (*init)(void); 
  void         (*main)(void const *arg);
  osPriority    priority;
  uint32_t      stack_size;
  osThreadDef_t thread_def;
  osThreadId    thread_id;
} thread_t;



namespace thread
{

bool init(void);

}
#endif