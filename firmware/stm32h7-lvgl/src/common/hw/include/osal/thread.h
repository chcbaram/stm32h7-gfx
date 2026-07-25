#ifndef THREAD_H_
#define THREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_THREAD

#define THREAD_MAX_CNT  HW_THREAD_MAX_CNT


typedef int16_t thread_id_t;


bool threadInit(void);
bool threadCreate(const char *name, void (*func)(void const *arg), void *arg, osPriority priority, uint32_t stack_bytes);
bool threadBegin(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
