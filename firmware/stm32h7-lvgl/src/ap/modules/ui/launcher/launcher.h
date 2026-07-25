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
void  launcherRebuildHome(void);      /* 테마 변경 등으로 홈 화면만 다시 그림 */

#endif

#ifdef __cplusplus
}
#endif

#endif
