#ifndef UI_SHADE_H_
#define UI_SHADE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_LVGL

/* 화면 상단을 쓸어내리면 내려오는 전역 설정 셰이드.
 * 최상위 레이어에 붙으므로 어느 app 화면에서도 동작한다.
 */
bool ui_shade_init(void);
bool ui_shade_is_open(void);
void ui_shade_apply_theme(void);   /* 테마 변경 후 셰이드 인라인 색 갱신 (UI 스레드) */

#endif

#ifdef __cplusplus
}
#endif

#endif
