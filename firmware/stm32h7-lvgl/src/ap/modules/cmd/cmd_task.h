#ifndef CMD_TASK_H_
#define CMD_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

bool cmdTaskInit(void);
bool cmdTaskUpdate(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
