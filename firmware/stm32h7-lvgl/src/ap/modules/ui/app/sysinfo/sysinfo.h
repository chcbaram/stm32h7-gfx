#ifndef SYSINFO_H_
#define SYSINFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"

#ifdef _USE_HW_LVGL

bool sysinfoInit(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
