#include "ui_theme.h"
#include "nvs.h"


#ifdef _USE_HW_LVGL

#define THEME_NVS_NAME   "ui.theme"


static bool is_init = false;

/* 다크/라이트 팔레트. 강조색(빨강)은 두 테마 공통으로 브랜드색을 유지한다. */
static const ui_palette_t palettes[UI_THEME_CNT] =
{
  [UI_THEME_DARK] = {
    .bg          = 0x0E1216,
    .surface     = 0x181D23,
    .surface_alt = 0x232A32,
    .line        = 0x2E3742,
    .text        = 0xF2F5F7,
    .text_dim    = 0x8A97A6,
    .text_on_acc = 0xF2F5F7,
    .accent      = 0xE8503A,
    .accent_dim  = 0x8E3225,
    .ok          = 0x4CC38A,
  },
  [UI_THEME_LIGHT] = {
    .bg          = 0xEEF1F5,
    .surface     = 0xFFFFFF,
    .surface_alt = 0xE4E8EE,
    .line        = 0xD0D7E0,
    .text        = 0x1A2028,
    .text_dim    = 0x66707C,
    .text_on_acc = 0xFFFFFF,
    .accent      = 0xE8503A,
    .accent_dim  = 0xC2402E,
    .ok          = 0x2FA36B,
  },
};

static ui_theme_mode_t theme_mode = UI_THEME_DARK;

const ui_palette_t *uiPalette(void)
{
  return &palettes[theme_mode];
}

ui_theme_mode_t uiThemeGetMode(void)
{
  return theme_mode;
}

static lv_style_t style_screen;
static lv_style_t style_card;
static lv_style_t style_btn;
static lv_style_t style_btn_pressed;
static lv_style_t style_btn_accent;
static lv_style_t style_btn_disabled;
static lv_style_t style_text_title;
static lv_style_t style_text_body;
static lv_style_t style_text_dim;




/* 영문은 montserrat, 한글 등 글리프가 없는 문자는 fallback(외부 한글 폰트)으로.
 * montserrat 원본은 const 라 fallback 을 못 넣으므로 복사본에 지정한다.
 */
static lv_font_t font_title;
static lv_font_t font_body;
static lv_font_t font_caption;

static const lv_font_t *kr_title_font   = NULL;
static const lv_font_t *kr_body_font    = NULL;
static const lv_font_t *kr_caption_font = NULL;

const lv_font_t *uiFontTitle(void)   { return &font_title; }
const lv_font_t *uiFontBody(void)    { return &font_body; }
const lv_font_t *uiFontCaption(void) { return &font_caption; }

/* 한글 fallback 폰트 지정. uiThemeInit 전에 호출한다. NULL 이면 영문만. */
void uiThemeSetKrFont(const lv_font_t *kr_title,
                      const lv_font_t *kr_body,
                      const lv_font_t *kr_caption)
{
  kr_title_font   = kr_title;
  kr_body_font    = kr_body;
  kr_caption_font = kr_caption;
}


/* 스타일 속성(색 포함)을 현재 팔레트로 (재)설정한다.
 * lv_style_init 은 최초 1회만 하고, 이 함수는 테마가 바뀔 때마다 다시 부른다.
 * (lv_style_set_* 은 기존 값을 덮어쓰므로 재호출해도 안전하다.)
 */
static void themeApplyStyles(void)
{
  /* --- 화면 --- */
  lv_style_set_bg_color(&style_screen, lv_color_hex(UI_COLOR_BG));
  lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
  lv_style_set_border_width(&style_screen, 0);
  lv_style_set_pad_all(&style_screen, 0);
  lv_style_set_text_color(&style_screen, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_screen, uiFontBody());

  /* --- 카드 --- */
  lv_style_set_bg_color(&style_card, lv_color_hex(UI_COLOR_SURFACE));
  lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
  lv_style_set_radius(&style_card, UI_RADIUS_MD);
  lv_style_set_border_color(&style_card, lv_color_hex(UI_COLOR_LINE));
  lv_style_set_border_width(&style_card, 1);
  lv_style_set_pad_all(&style_card, UI_SPACE_MD);

  /* --- 버튼 ---
   * 그림자는 LVGL 이 소프트웨어로 블러하므로 480x480 에서 비싸다.
   * 대신 면색과 경계선으로 층을 구분한다.
   */
  lv_style_set_bg_color(&style_btn, lv_color_hex(UI_COLOR_SURFACE_ALT));
  lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
  lv_style_set_radius(&style_btn, UI_RADIUS_SM);
  lv_style_set_border_color(&style_btn, lv_color_hex(UI_COLOR_LINE));
  lv_style_set_border_width(&style_btn, 1);
  lv_style_set_text_color(&style_btn, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_btn, uiFontBody());
  lv_style_set_shadow_width(&style_btn, 0);

  lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(UI_COLOR_LINE));
  lv_style_set_translate_y(&style_btn_pressed, 2);

  lv_style_set_bg_color(&style_btn_accent, lv_color_hex(UI_COLOR_ACCENT));
  lv_style_set_border_width(&style_btn_accent, 0);

  /* 못 누르는 버튼. LV_STATE_DISABLED 를 걸어도 보이는 게 그대로면 사람은
     누를 수 있다고 생각하고 누른다. 강조 버튼도 덮어야 하므로 accent 뒤에
     붙인다. */
  lv_style_set_bg_color(&style_btn_disabled, lv_color_hex(UI_COLOR_SURFACE_ALT));
  lv_style_set_bg_opa(&style_btn_disabled, LV_OPA_COVER);
  lv_style_set_text_color(&style_btn_disabled, lv_color_hex(UI_COLOR_TEXT_DIM));
  lv_style_set_border_color(&style_btn_disabled, lv_color_hex(UI_COLOR_LINE));
  lv_style_set_border_width(&style_btn_disabled, 1);
  lv_style_set_translate_y(&style_btn_disabled, 0);
  lv_style_set_text_color(&style_btn_accent, lv_color_hex(UI_COLOR_TEXT_ON_ACC));

  /* --- 글자 --- */
  lv_style_set_text_color(&style_text_title, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_text_title, uiFontTitle());

  lv_style_set_text_color(&style_text_body, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_text_body, uiFontBody());

  lv_style_set_text_color(&style_text_dim, lv_color_hex(UI_COLOR_TEXT_DIM));
  lv_style_set_text_font(&style_text_dim, uiFontCaption());
}

bool uiThemeInit(void)
{
  ui_theme_mode_t m;

  if (is_init == true)
    return true;

  /* 저장된 테마를 불러온다 (없으면 다크). */
  if (nvsGet(THEME_NVS_NAME, &m, sizeof(m)) == true && m < UI_THEME_CNT)
    theme_mode = m;

  /* 역할 폰트 = montserrat 복사본 + 한글 fallback */
  font_title   = lv_font_montserrat_40;
  font_body    = lv_font_montserrat_28;
  font_caption = lv_font_montserrat_20;
  font_title.fallback   = kr_title_font;
  font_body.fallback    = kr_body_font;
  font_caption.fallback = kr_caption_font;

  /* 스타일은 여기서 딱 한 번만 init 한다. (재테마 시엔 속성만 다시 세팅) */
  lv_style_init(&style_screen);
  lv_style_init(&style_card);
  lv_style_init(&style_btn);
  lv_style_init(&style_btn_pressed);
  lv_style_init(&style_btn_accent);
  lv_style_init(&style_btn_disabled);
  lv_style_init(&style_text_title);
  lv_style_init(&style_text_body);
  lv_style_init(&style_text_dim);

  themeApplyStyles();

  is_init = true;
  return true;
}

void uiThemeSetMode(ui_theme_mode_t mode)
{
  ui_theme_mode_t m = mode;

  if (mode >= UI_THEME_CNT || mode == theme_mode)
    return;

  theme_mode = mode;

  /* 공유 스타일의 색을 새 팔레트로 다시 세팅하고 전 위젯에 반영한다.
   * (인라인 색을 쓰는 홈 상단바/셰이드는 각 모듈이 따로 갱신한다.)
   */
  if (is_init == true)
  {
    themeApplyStyles();
    lv_obj_report_style_change(NULL);
  }

  nvsSet(THEME_NVS_NAME, &m, sizeof(m));
}

void uiThemeToggle(void)
{
  uiThemeSetMode(theme_mode == UI_THEME_DARK ? UI_THEME_LIGHT : UI_THEME_DARK);
}

lv_style_t *uiStyleScreen(void)     { return &style_screen; }
lv_style_t *uiStyleCard(void)       { return &style_card; }
lv_style_t *uiStyleBtn(void)        { return &style_btn; }
lv_style_t *uiStyleBtnPressed(void) { return &style_btn_pressed; }
lv_style_t *uiStyleBtnAccent(void)  { return &style_btn_accent; }
lv_style_t *uiStyleBtnDisabled(void){ return &style_btn_disabled; }
lv_style_t *uiStyleTextTitle(void)  { return &style_text_title; }
lv_style_t *uiStyleTextBody(void)   { return &style_text_body; }
lv_style_t *uiStyleTextDim(void)    { return &style_text_dim; }


lv_obj_t *uiCreateScreen(lv_obj_t *scr)
{
  lv_obj_add_style(scr, uiStyleScreen(), LV_PART_MAIN);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

lv_obj_t *uiCreateCard(lv_obj_t *parent, int32_t w, int32_t h)
{
  lv_obj_t *obj = lv_obj_create(parent);

  lv_obj_remove_style_all(obj);
  lv_obj_add_style(obj, uiStyleCard(), LV_PART_MAIN);
  lv_obj_set_size(obj, w, h);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

lv_obj_t *uiCreateButton(lv_obj_t *parent, const char *text, bool accent)
{
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *label;

  lv_obj_remove_style_all(btn);
  lv_obj_add_style(btn, uiStyleBtn(), LV_PART_MAIN);
  lv_obj_add_style(btn, uiStyleBtnPressed(), LV_PART_MAIN | LV_STATE_PRESSED);
  if (accent == true)
  {
    lv_obj_add_style(btn, uiStyleBtnAccent(), LV_PART_MAIN);
  }
  // 비활성은 마지막에 붙여야 강조색을 덮는다
  lv_obj_add_style(btn, uiStyleBtnDisabled(), LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_set_size(btn, UI_TOUCH_MIN, UI_TOUCH_MIN);

  if (text != NULL)
  {
    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
  }
  return btn;
}

lv_obj_t *uiCreateLabel(lv_obj_t *parent, const char *text, lv_style_t *style)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_obj_add_style(label, style != NULL ? style : uiStyleTextBody(), LV_PART_MAIN);
  lv_label_set_text(label, text);
  return label;
}

lv_obj_t *uiCreateBackButton(lv_obj_t *parent, lv_event_cb_t cb)
{
  lv_obj_t *btn = uiCreateButton(parent, LV_SYMBOL_LEFT, false);

  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  return btn;
}

#endif
