#ifndef LVGL_H_
#define LVGL_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_LVGL
#include "lvgl/lvgl.h"


bool lvglInit(void);
bool lvglUpdate(void);
bool lvglSetEnable(bool enable);
bool lvglGetEnable(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
