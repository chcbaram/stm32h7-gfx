#include "ui_theme.h"


#ifdef _USE_HW_LVGL

static bool is_init = false;

static lv_style_t style_screen;
static lv_style_t style_card;
static lv_style_t style_btn;
static lv_style_t style_btn_pressed;
static lv_style_t style_btn_accent;
static lv_style_t style_text_title;
static lv_style_t style_text_body;
static lv_style_t style_text_dim;




/* 4.0인치 480x480 (약 170 PPI) 을 팔 뻗은 거리에서 보는 기준으로 잡았다.
 * 한 단계씩 더 키우려면 여기만 고치면 화면 전체에 반영된다.
 */
const lv_font_t *uiFontDisplay(void) { return &lv_font_montserrat_48; }
const lv_font_t *uiFontTitle(void)   { return &lv_font_montserrat_40; }
const lv_font_t *uiFontBody(void)    { return &lv_font_montserrat_28; }
const lv_font_t *uiFontCaption(void) { return &lv_font_montserrat_20; }


bool uiThemeInit(void)
{
  if (is_init == true)
    return true;

  /* --- 화면 --- */
  lv_style_init(&style_screen);
  lv_style_set_bg_color(&style_screen, lv_color_hex(UI_COLOR_BG));
  lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
  lv_style_set_border_width(&style_screen, 0);
  lv_style_set_pad_all(&style_screen, 0);
  lv_style_set_text_color(&style_screen, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_screen, uiFontBody());

  /* --- 카드 --- */
  lv_style_init(&style_card);
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
  lv_style_init(&style_btn);
  lv_style_set_bg_color(&style_btn, lv_color_hex(UI_COLOR_SURFACE_ALT));
  lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
  lv_style_set_radius(&style_btn, UI_RADIUS_SM);
  lv_style_set_border_color(&style_btn, lv_color_hex(UI_COLOR_LINE));
  lv_style_set_border_width(&style_btn, 1);
  lv_style_set_text_color(&style_btn, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_btn, uiFontBody());
  lv_style_set_shadow_width(&style_btn, 0);

  lv_style_init(&style_btn_pressed);
  lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(UI_COLOR_LINE));
  lv_style_set_translate_y(&style_btn_pressed, 2);

  lv_style_init(&style_btn_accent);
  lv_style_set_bg_color(&style_btn_accent, lv_color_hex(UI_COLOR_ACCENT));
  lv_style_set_border_width(&style_btn_accent, 0);
  lv_style_set_text_color(&style_btn_accent, lv_color_hex(UI_COLOR_TEXT_ON_ACC));

  /* --- 글자 --- */
  lv_style_init(&style_text_title);
  lv_style_set_text_color(&style_text_title, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_text_title, uiFontTitle());

  lv_style_init(&style_text_body);
  lv_style_set_text_color(&style_text_body, lv_color_hex(UI_COLOR_TEXT));
  lv_style_set_text_font(&style_text_body, uiFontBody());

  lv_style_init(&style_text_dim);
  lv_style_set_text_color(&style_text_dim, lv_color_hex(UI_COLOR_TEXT_DIM));
  lv_style_set_text_font(&style_text_dim, uiFontCaption());

  is_init = true;
  return true;
}

lv_style_t *uiStyleScreen(void)     { return &style_screen; }
lv_style_t *uiStyleCard(void)       { return &style_card; }
lv_style_t *uiStyleBtn(void)        { return &style_btn; }
lv_style_t *uiStyleBtnPressed(void) { return &style_btn_pressed; }
lv_style_t *uiStyleBtnAccent(void)  { return &style_btn_accent; }
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
