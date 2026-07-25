#ifndef UI_THEME_H_
#define UI_THEME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_LVGL

/* 화면에 표시할 제품 이름. 부팅 로그의 _DEF_BOARD_NAME 과는 별개다. */
#define UI_TITLE   "STM32H7-LVGL"

/* ---------------------------------------------------------------------------
 * 색
 *
 * 화면에 쓰는 색은 전부 여기서만 정의한다.
 * 위젯마다 lv_color_hex() 를 흩뿌리면 통일감이 무너진다.
 * ------------------------------------------------------------------------- */
#define UI_COLOR_BG           0x0E1216   /* 화면 바탕            */
#define UI_COLOR_SURFACE      0x181D23   /* 카드/패널            */
#define UI_COLOR_SURFACE_ALT  0x232A32   /* 눌린 상태, 보조 면   */
#define UI_COLOR_LINE         0x2E3742   /* 경계선               */

#define UI_COLOR_TEXT         0xF2F5F7   /* 본문                 */
#define UI_COLOR_TEXT_DIM     0x8A97A6   /* 보조 설명            */
#define UI_COLOR_TEXT_ON_ACC  0x0E1216   /* 강조색 위의 글자     */

#define UI_COLOR_ACCENT       0xE8503A   /* 강조 (재생/녹음)     */
#define UI_COLOR_ACCENT_DIM   0x8E3225
#define UI_COLOR_OK           0x4CC38A

/* ---------------------------------------------------------------------------
 * 간격  (4 배수 스케일)
 * ------------------------------------------------------------------------- */
#define UI_SPACE_XS           4
#define UI_SPACE_SM           8
#define UI_SPACE_MD           16
#define UI_SPACE_LG           24
#define UI_SPACE_XL           40

#define UI_RADIUS_SM          8
#define UI_RADIUS_MD          14
#define UI_RADIUS_LG          22

/* 화면 가장자리 여백 */
#define UI_MARGIN             UI_SPACE_LG

/* 손가락으로 누를 수 있는 최소 크기 */
#define UI_TOUCH_MIN          64


/* ---------------------------------------------------------------------------
 * 폰트 역할
 *
 * 크기를 코드에 직접 쓰지 않고 역할로 부른다.
 * 나중에 한글 폰트로 교체할 때 여기만 바꾸면 된다.
 * ------------------------------------------------------------------------- */
const lv_font_t *uiFontTitle(void);     /* 28 - 화면 제목       */
const lv_font_t *uiFontBody(void);      /* 20 - 본문, 버튼      */
const lv_font_t *uiFontCaption(void);   /* 14 - 라벨, 부가정보  */


/* ---------------------------------------------------------------------------
 * 공유 스타일
 *
 * lv_style_t 는 한 번만 만들어 여러 위젯이 함께 쓴다.
 * 위젯마다 인라인으로 스타일을 박으면 RAM 도 낭비된다.
 * ------------------------------------------------------------------------- */
bool uiThemeInit(void);
void uiThemeSetKrFont(const lv_font_t *kr);   /* 한글 fallback 폰트 (uiThemeInit 전에) */

lv_style_t *uiStyleScreen(void);        /* 화면 바탕            */
lv_style_t *uiStyleCard(void);          /* 패널/카드            */
lv_style_t *uiStyleBtn(void);           /* 일반 버튼            */
lv_style_t *uiStyleBtnPressed(void);    /* 버튼 눌림            */
lv_style_t *uiStyleBtnAccent(void);     /* 강조 버튼            */
lv_style_t *uiStyleTextTitle(void);
lv_style_t *uiStyleTextBody(void);
lv_style_t *uiStyleTextDim(void);


/* 자주 쓰는 조합 도우미 */
lv_obj_t *uiCreateScreen(lv_obj_t *scr);
lv_obj_t *uiCreateCard(lv_obj_t *parent, int32_t w, int32_t h);
lv_obj_t *uiCreateButton(lv_obj_t *parent, const char *text, bool accent);
lv_obj_t *uiCreateLabel(lv_obj_t *parent, const char *text, lv_style_t *style);
lv_obj_t *uiCreateBackButton(lv_obj_t *parent, lv_event_cb_t cb);

#endif

#ifdef __cplusplus
}
#endif

#endif
