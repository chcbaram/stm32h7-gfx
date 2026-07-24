#include "ui_shade.h"
#include "ui_theme.h"

#ifdef _USE_HW_LVGL
#ifdef _USE_HW_I2S
#include "i2s.h"
#endif


#define PANEL_H        220
#define SENSOR_H       48
#define ANIM_MS        220
#define OPEN_SNAP      (PANEL_H / 3)   /* 이만큼 내려오면 열림으로 스냅 */


static void shadeAnimTo(int32_t y_to, bool open);
static void sensorPressCb(lv_event_t *e);
static void panelPressCb(lv_event_t *e);
static void dragReleaseCb(lv_event_t *e);
static void backdropClickCb(lv_event_t *e);
static void volSliderCb(lv_event_t *e);
static void animYCb(void *obj, int32_t v);
static int32_t touchY(void);


static lv_obj_t *panel     = NULL;
static lv_obj_t *backdrop  = NULL;
static lv_obj_t *label_vol = NULL;
static bool      is_open   = false;
static int32_t   drag_base = 0;       /* 드래그 시작 시 패널 y */
static int32_t   drag_y0   = 0;       /* 드래그 시작 시 터치 y */
static bool      dragging  = false;




bool ui_shade_init(void)
{
  lv_obj_t *top = lv_layer_top();
  lv_obj_t *sensor;
  lv_obj_t *handle;
  lv_obj_t *title;
  lv_obj_t *icon;
  lv_obj_t *slider;
  int vol = 50;

#ifdef _USE_HW_I2S
  vol = i2sGetVolume();
#endif

  lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);

  /* --- 뒤 배경 (열렸을 때만) --- */
  backdrop = lv_obj_create(top);
  lv_obj_remove_style_all(backdrop);
  lv_obj_set_size(backdrop, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, LV_PART_MAIN);
  lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(backdrop, backdropClickCb, LV_EVENT_CLICKED, NULL);

  /* --- 상단 감지 스트립 --- */
  sensor = lv_obj_create(top);
  lv_obj_remove_style_all(sensor);
  lv_obj_set_size(sensor, LCD_WIDTH, SENSOR_H);
  lv_obj_align(sensor, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_add_flag(sensor, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sensor, sensorPressCb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(sensor, sensorPressCb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(sensor, dragReleaseCb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(sensor, dragReleaseCb, LV_EVENT_PRESS_LOST, NULL);

  /* --- 셰이드 패널 --- */
  panel = lv_obj_create(top);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, LCD_WIDTH, PANEL_H);
  lv_obj_set_pos(panel, 0, -PANEL_H);
  lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_color(panel, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(panel, UI_MARGIN, LV_PART_MAIN);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(panel, panelPressCb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(panel, panelPressCb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(panel, dragReleaseCb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(panel, dragReleaseCb, LV_EVENT_PRESS_LOST, NULL);

  title = uiCreateLabel(panel, "Settings", uiStyleTextTitle());
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  /* --- 볼륨 --- */
  icon = lv_label_create(panel);
  lv_label_set_text(icon, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_MAIN);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 20);

  slider = lv_slider_create(panel);
  lv_obj_set_width(slider, LCD_WIDTH - UI_MARGIN*2 - 110);
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, 20);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, vol, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
  lv_obj_add_event_cb(slider, volSliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(slider, volSliderCb, LV_EVENT_RELEASED, NULL);

  label_vol = uiCreateLabel(panel, "", uiStyleTextBody());
  lv_label_set_text_fmt(label_vol, "%d", vol);
  lv_obj_align(label_vol, LV_ALIGN_RIGHT_MID, 0, 20);

  /* 아래쪽 손잡이 */
  handle = lv_obj_create(panel);
  lv_obj_remove_style_all(handle);
  lv_obj_set_size(handle, 60, 5);
  lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 6);
  lv_obj_set_style_radius(handle, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(handle, lv_color_hex(UI_COLOR_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, LV_PART_MAIN);

  is_open = false;
  return true;
}

bool ui_shade_is_open(void)
{
  return is_open;
}

int32_t touchY(void)
{
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  return p.y;
}

/* 상단 스트립을 눌러 아래로 끌면 패널이 손가락을 따라 내려온다. */
void sensorPressCb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED)
  {
    dragging  = true;
    drag_base = lv_obj_get_y(panel);   /* 보통 -PANEL_H */
    drag_y0   = touchY();
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
  }
  else if (code == LV_EVENT_PRESSING && dragging == true)
  {
    int32_t y = drag_base + (touchY() - drag_y0);
    if (y < -PANEL_H) y = -PANEL_H;
    if (y > 0)        y = 0;
    lv_obj_set_y(panel, y);
  }
}

/* 열린 패널을 눌러 위로 끌면 닫힌다. (슬라이더 조작과 구분: 위로 이동할 때만) */
void panelPressCb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED)
  {
    drag_base = lv_obj_get_y(panel);
    drag_y0   = touchY();
    dragging  = false;   /* 슬라이더일 수 있으니 아직 드래그로 확정 안 함 */
  }
  else if (code == LV_EVENT_PRESSING)
  {
    int32_t dy = touchY() - drag_y0;

    /* 위로 20px 이상 끌면 패널 드래그로 본다. */
    if (dragging == false && dy < -20)
      dragging = true;

    if (dragging == true)
    {
      int32_t y = drag_base + dy;
      if (y < -PANEL_H) y = -PANEL_H;
      if (y > 0)        y = 0;
      lv_obj_set_y(panel, y);
    }
  }
}

void dragReleaseCb(lv_event_t *e)
{
  LV_UNUSED(e);

  if (dragging == false)
    return;
  dragging = false;

  /* 절반 넘게 내려와 있으면 열고, 아니면 닫는다. */
  if (lv_obj_get_y(panel) > -PANEL_H + OPEN_SNAP)
    shadeAnimTo(0, true);
  else
    shadeAnimTo(-PANEL_H, false);
}

void shadeAnimTo(int32_t y_to, bool open)
{
  lv_anim_t a;

  is_open = open;
  if (open == false)
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);

  lv_anim_init(&a);
  lv_anim_set_var(&a, panel);
  lv_anim_set_exec_cb(&a, animYCb);
  lv_anim_set_values(&a, lv_obj_get_y(panel), y_to);
  lv_anim_set_duration(&a, ANIM_MS);
  lv_anim_set_path_cb(&a, open ? lv_anim_path_ease_out : lv_anim_path_ease_in);
  lv_anim_start(&a);
}

void animYCb(void *obj, int32_t v)
{
  lv_obj_set_y((lv_obj_t *)obj, v);
}

void backdropClickCb(lv_event_t *e)
{
  LV_UNUSED(e);
  shadeAnimTo(-PANEL_H, false);
}

void volSliderCb(lv_event_t *e)
{
  lv_obj_t       *slider = lv_event_get_target_obj(e);
  lv_event_code_t code   = lv_event_get_code(e);
  int vol = lv_slider_get_value(slider);

#ifdef _USE_HW_I2S
  if (code == LV_EVENT_VALUE_CHANGED)
  {
    i2sSetVolume(vol);
    lv_label_set_text_fmt(label_vol, "%d", vol);
  }
  else if (code == LV_EVENT_RELEASED)
  {
    /* 드래그를 놓을 때만 NVS 에 저장한다 (매 변경마다 저장하면 혹사). */
    i2sCfgSave();
  }
#else
  lv_label_set_text_fmt(label_vol, "%d", vol);
#endif
}

#endif
