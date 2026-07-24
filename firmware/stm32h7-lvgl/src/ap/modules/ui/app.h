#ifndef APP_H_
#define APP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_LVGL

/* app 정의
 *
 * 각 app 은 .app 섹션에 자기 자신을 등록하므로 런처에 목록을 손으로
 * 나열할 필요가 없다. app 을 추가하려면 .c 파일 하나만 넣으면 된다.
 *
 * 루프는 런처가 돈다. app 은 enter() 에서 받은 화면에 UI 를 구성하고
 * update() 에서 주기 작업만 하면 된다. 화면 객체의 생성/삭제는 런처가
 * 책임지므로 app 이 화면을 흘릴 수 없다.
 */
typedef struct app_info_t_
{
  const char name[32];
  uint16_t   order;                    /* 런처에 표시할 순서 (작을수록 앞) */

  bool (*init)(void);                  /* 부팅 시 1회. 가벼운 준비만 한다.  */
  bool (*enter)(lv_obj_t *scr);        /* app 진입. scr 에 UI 를 구성한다.  */
  void (*update)(void);                /* 런처 루프에서 주기 호출           */
  void (*exit)(void);                  /* app 종료. 화면 외 자원을 정리한다.*/

} app_info_t;

#define APP_DEF(x_name) static __attribute__((section(".app"))) volatile app_info_t app_##x_name =

#endif

#ifdef __cplusplus
}
#endif

#endif
