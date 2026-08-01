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
/* 색은 런타임 팔레트에서 읽는다. 다크/라이트 테마를 토글로 바꾸기 위해서다.
 * 이름은 그대로라 기존 lv_color_hex(UI_COLOR_*) 사용부는 수정할 필요가 없다.
 */
typedef enum
{
  UI_THEME_DARK = 0,
  UI_THEME_LIGHT,
  UI_THEME_CNT,
} ui_theme_mode_t;

typedef struct
{
  uint32_t bg;            /* 화면 바탕            */
  uint32_t surface;       /* 카드/패널            */
  uint32_t surface_alt;   /* 눌린 상태, 보조 면   */
  uint32_t line;          /* 경계선               */
  uint32_t text;          /* 본문                 */
  uint32_t text_dim;      /* 보조 설명            */
  uint32_t text_on_acc;   /* 강조색 위의 글자     */
  uint32_t accent;        /* 강조 (재생/녹음)     */
  uint32_t accent_dim;
  uint32_t ok;
} ui_palette_t;

const ui_palette_t *uiPalette(void);          /* 현재 활성 팔레트 */
ui_theme_mode_t     uiThemeGetMode(void);
void                uiThemeSetMode(ui_theme_mode_t mode);  /* 스타일 재적용 + 저장 */
void                uiThemeToggle(void);       /* 다크 <-> 라이트 */

#define UI_COLOR_BG           (uiPalette()->bg)
#define UI_COLOR_SURFACE      (uiPalette()->surface)
#define UI_COLOR_SURFACE_ALT  (uiPalette()->surface_alt)
#define UI_COLOR_LINE         (uiPalette()->line)

#define UI_COLOR_TEXT         (uiPalette()->text)
#define UI_COLOR_TEXT_DIM     (uiPalette()->text_dim)
#define UI_COLOR_TEXT_ON_ACC  (uiPalette()->text_on_acc)

#define UI_COLOR_ACCENT       (uiPalette()->accent)
#define UI_COLOR_ACCENT_DIM   (uiPalette()->accent_dim)
#define UI_COLOR_OK           (uiPalette()->ok)

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
/* 한글 fallback 폰트. 역할마다 따로 받는다 (uiThemeInit 전에).

   fallback 은 크기를 안 바꾸고 그 폰트의 원래 크기로 그린다. 28px 한글 하나로
   20px 캡션까지 덮으면 한글만 40% 크게 나와 줄을 넘친다. 실제로 그랬다.
   NULL 을 주면 그 역할은 영문만 나온다. */
void                uiThemeSetKrFont(const lv_font_t *kr_title,
                                     const lv_font_t *kr_body,
                                     const lv_font_t *kr_caption);

lv_style_t *uiStyleScreen(void);        /* 화면 바탕            */
lv_style_t *uiStyleCard(void);          /* 패널/카드            */
lv_style_t *uiStyleBtn(void);           /* 일반 버튼            */
lv_style_t *uiStyleBtnPressed(void);    /* 버튼 눌림            */
lv_style_t *uiStyleBtnAccent(void);     /* 강조 버튼            */
lv_style_t *uiStyleBtnDisabled(void);   /* 못 누르는 버튼       */
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
