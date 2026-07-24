#ifndef LAUNCHER_H_
#define LAUNCHER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_LVGL

bool  launcherInit(void);
void  launcherUpdate(void);

bool  launcherRunApp(const char *name);
void  launcherExitApp(void);          /* app 이 스스로 빠져나올 때 호출한다 */
const char *launcherGetAppName(void); /* 실행중인 app 이 없으면 NULL        */

#endif

#ifdef __cplusplus
}
#endif

#endif
