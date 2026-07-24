#include "cassette.h"
#include "cassette_audio.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

#define REEL_SPOKE_CNT      3
#define REEL_HUB_R          16
#define REEL_PACK_R_MIN     22
#define REEL_PACK_R_MAX     46
#define REEL_GAP            118         /* 좌우 릴 중심 간격의 절반 */

#define SPEC_BAR_W          7
#define SPEC_BAR_GAP        3
#define SPEC_MAX_H          88

/* 막대 높이(레벨)에 따라 색이 바뀐다 (낮음 초록 → 중간 노랑 → 높음 빨강) */
#define SPEC_COL_LO         0x3DDC5A
#define SPEC_COL_MID        0xE8C13A
#define SPEC_COL_HI         0xE8503A


typedef struct
{
  lv_obj_t *pack;
  lv_obj_t *hub;
  lv_obj_t *spoke[REEL_SPOKE_CNT];
  int32_t   cx;
  int32_t   cy;
} reel_t;


static bool cassetteEnter(lv_obj_t *scr);
static void cassetteUpdate(void);
static void cassetteExit(void);

static void createShell(lv_obj_t *parent);
static void createTransport(lv_obj_t *parent);
static void createReel(lv_obj_t *parent, reel_t *p_reel, int32_t cx, int32_t cy);
static void createSpectrum(lv_obj_t *parent);
static int      specColorLevel(int v);
static uint32_t specColor(int level);
static void updateReel(reel_t *p_reel, int32_t angle, int32_t pack_r);
static void refreshTape(void);
static void btnPlayCb(lv_event_t *e);
static void btnStopCb(lv_event_t *e);
static void btnPrevCb(lv_event_t *e);
static void btnNextCb(lv_event_t *e);
static void btnRecCb(lv_event_t *e);


static reel_t    reel_l;
static reel_t    reel_r;
static lv_obj_t *label_title;
static lv_obj_t *label_state;
static lv_obj_t *label_time;
static lv_obj_t *bar_progress;
static lv_obj_t *spec_bar[CASS_SPECTRUM_BARS];
static int8_t    spec_col_pre[CASS_SPECTRUM_BARS];

static int32_t   reel_angle = 0;
static int       sel_idx = 0;




bool cassetteInit(void)
{
  return cassetteAudioInit();
}

bool cassetteEnter(lv_obj_t *scr)
{
  cassetteAudioScan();

  createShell(scr);
  createTransport(scr);

  reel_angle = 0;
  sel_idx    = 0;
  if (cassetteAudioGetIndex() >= 0)
    sel_idx = cassetteAudioGetIndex();

  refreshTape();
  return true;
}

void cassetteExit(void)
{
  cassetteAudioStop();
}

void cassetteUpdate(void)
{
  CassAudioState_t st = cassetteAudioGetState();
  uint32_t pos = cassetteAudioGetPosMs();
  uint32_t dur = cassetteAudioGetDurMs();
  int32_t  pack_l;
  int32_t  pack_r;
  int32_t  ratio;               /* 0..1000 */
  uint8_t  bars[CASS_SPECTRUM_BARS];


  /* 재생/녹음 중이면 릴을 돌린다. */
  if (st != CASS_AUDIO_IDLE)
  {
    reel_angle = (reel_angle + 12) % 360;
  }

  ratio = (dur > 0) ? (int32_t)((uint64_t)pos * 1000 / dur) : 0;
  if (ratio > 1000) ratio = 1000;

  pack_l = REEL_PACK_R_MAX - ((REEL_PACK_R_MAX - REEL_PACK_R_MIN) * ratio) / 1000;
  pack_r = REEL_PACK_R_MIN + ((REEL_PACK_R_MAX - REEL_PACK_R_MIN) * ratio) / 1000;

  updateReel(&reel_l, reel_angle, pack_l);
  updateReel(&reel_r, reel_angle, pack_r);

  /* 진행바 */
  lv_obj_set_width(bar_progress, (LCD_WIDTH - UI_MARGIN*2 - UI_SPACE_MD*2) * ratio / 1000);

  /* 시간 : 경과 / 전체 */
  lv_label_set_text_fmt(label_time, "%02d:%02d / %02d:%02d",
                        (int)(pos/1000)/60, (int)(pos/1000)%60,
                        (int)(dur/1000)/60, (int)(dur/1000)%60);

  /* 스펙트럼 : 중앙에서 상하로 커지는 막대, 높이에 따라 색이 바뀐다 */
  cassetteAudioGetSpectrum(bars, CASS_SPECTRUM_BARS);
  for (int i = 0; i < CASS_SPECTRUM_BARS; i++)
  {
    int h   = 2 + (SPEC_MAX_H - 2) * bars[i] / 100;
    int col = specColorLevel(bars[i]);

    lv_obj_set_height(spec_bar[i], h);

    if (col != spec_col_pre[i])
    {
      lv_obj_set_style_bg_color(spec_bar[i], lv_color_hex((uint32_t)specColor(col)), LV_PART_MAIN);
      spec_col_pre[i] = col;
    }
  }

  /* 상태 라벨은 오디오 스레드가 스스로 IDLE 로 돌아갈 수 있으므로 매번 반영 */
  {
    const char *s = (st == CASS_AUDIO_PLAY) ? "PLAY" :
                    (st == CASS_AUDIO_REC)  ? "REC"  : "STOP";
    lv_label_set_text(label_state, s);
    lv_obj_set_style_text_color(label_state,
                                st == CASS_AUDIO_REC ? lv_color_hex(UI_COLOR_ACCENT)
                                                     : lv_color_hex(0x6B6B6B),
                                LV_PART_MAIN);
  }
}

void createShell(lv_obj_t *parent)
{
  lv_obj_t *shell;
  lv_obj_t *sticker;
  lv_obj_t *window;
  lv_obj_t *track;


  /* --- 카세트 셸 --- */
  /* 버튼 행(하단) 위 공간의 한가운데에 놓아 위아래 여백을 맞춘다. */
  shell = uiCreateCard(parent, LCD_WIDTH - UI_MARGIN*2, 288);
  lv_obj_align(shell, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_radius(shell, UI_RADIUS_LG, LV_PART_MAIN);
  lv_obj_set_style_pad_all(shell, UI_SPACE_MD, LV_PART_MAIN);

  /* --- 종이 스티커 --- */
  sticker = lv_obj_create(shell);
  lv_obj_remove_style_all(sticker);
  lv_obj_set_size(sticker, LV_PCT(100), 92);
  lv_obj_align(sticker, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(sticker, UI_RADIUS_SM, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sticker, lv_color_hex(0xE9E4D6), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sticker, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sticker, UI_SPACE_SM + 2, LV_PART_MAIN);
  lv_obj_remove_flag(sticker, LV_OBJ_FLAG_SCROLLABLE);

  label_title = lv_label_create(sticker);
  lv_label_set_text(label_title, "No Tape");
  lv_obj_set_style_text_font(label_title, uiFontBody(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label_title, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
  lv_label_set_long_mode(label_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label_title, LV_PCT(100));
  lv_obj_align(label_title, LV_ALIGN_TOP_LEFT, 0, 0);

  label_state = lv_label_create(sticker);
  lv_label_set_text(label_state, "STOP");
  lv_obj_set_style_text_font(label_state, uiFontCaption(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label_state, lv_color_hex(0x6B6B6B), LV_PART_MAIN);
  lv_obj_align(label_state, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  label_time = lv_label_create(sticker);
  lv_label_set_text(label_time, "00:00 / 00:00");
  lv_obj_set_style_text_font(label_time, uiFontCaption(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label_time, lv_color_hex(0x6B6B6B), LV_PART_MAIN);
  lv_obj_align(label_time, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  /* --- 테이프 창 (릴 + 중앙 스펙트럼) --- */
  window = lv_obj_create(shell);
  lv_obj_remove_style_all(window);
  lv_obj_set_size(window, LV_PCT(100), 148);
  lv_obj_align(window, LV_ALIGN_TOP_MID, 0, 100);
  lv_obj_set_style_radius(window, UI_RADIUS_SM, LV_PART_MAIN);
  lv_obj_set_style_bg_color(window, lv_color_hex(0x0A0D10), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(window, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(window, LV_OBJ_FLAG_SCROLLABLE);

  createReel(window, &reel_l, -REEL_GAP, 0);
  createReel(window, &reel_r,  REEL_GAP, 0);
  createSpectrum(window);

  /* --- 진행바 --- */
  track = lv_obj_create(shell);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, LV_PCT(100), 6);
  lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(track, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(track, lv_color_hex(UI_COLOR_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

  bar_progress = lv_obj_create(track);
  lv_obj_remove_style_all(bar_progress);
  lv_obj_set_size(bar_progress, 0, 6);
  lv_obj_align(bar_progress, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(bar_progress, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_progress, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar_progress, LV_OPA_COVER, LV_PART_MAIN);
}

/* 막대 레벨(0..100) -> 색 구간 (0 초록 / 1 노랑 / 2 빨강) */
static int specColorLevel(int v)
{
  if (v >= 80) return 2;
  if (v >= 55) return 1;
  return 0;
}

static uint32_t specColor(int level)
{
  if (level >= 2) return SPEC_COL_HI;
  if (level >= 1) return SPEC_COL_MID;
  return SPEC_COL_LO;
}

void createSpectrum(lv_obj_t *parent)
{
  int total_w = CASS_SPECTRUM_BARS * SPEC_BAR_W + (CASS_SPECTRUM_BARS - 1) * SPEC_BAR_GAP;
  int x0 = -total_w / 2 + SPEC_BAR_W / 2;

  for (int i = 0; i < CASS_SPECTRUM_BARS; i++)
  {
    spec_col_pre[i] = -1;

    spec_bar[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(spec_bar[i]);
    lv_obj_set_size(spec_bar[i], SPEC_BAR_W, 2);
    lv_obj_align(spec_bar[i], LV_ALIGN_CENTER, x0 + i * (SPEC_BAR_W + SPEC_BAR_GAP), 0);
    lv_obj_set_style_radius(spec_bar[i], 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(spec_bar[i], lv_color_hex(SPEC_COL_LO), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(spec_bar[i], LV_OPA_COVER, LV_PART_MAIN);
  }
}

void createTransport(lv_obj_t *parent)
{
  const char   *txt[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_STOP, LV_SYMBOL_NEXT, "REC"};
  lv_event_cb_t cb[]  = {btnPrevCb, btnPlayCb, btnStopCb, btnNextCb, btnRecCb};
  lv_obj_t     *row;


  /* 재생 조작 4개 + 녹음(REC) 을 한 줄로. REC 만 강조색. */
  row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LCD_WIDTH - UI_MARGIN*2, UI_TOUCH_MIN);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -UI_SPACE_XL);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 5; i++)
  {
    lv_obj_t *btn = uiCreateButton(row, txt[i], i == 4);
    lv_obj_set_size(btn, 76, UI_TOUCH_MIN);
    lv_obj_add_event_cb(btn, cb[i], LV_EVENT_CLICKED, NULL);
  }
}

void createReel(lv_obj_t *parent, reel_t *p_reel, int32_t cx, int32_t cy)
{
  p_reel->cx = cx;
  p_reel->cy = cy;

  p_reel->pack = lv_obj_create(parent);
  lv_obj_remove_style_all(p_reel->pack);
  lv_obj_set_size(p_reel->pack, REEL_PACK_R_MIN*2, REEL_PACK_R_MIN*2);
  lv_obj_align(p_reel->pack, LV_ALIGN_CENTER, cx, cy);
  lv_obj_set_style_radius(p_reel->pack, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(p_reel->pack, lv_color_hex(0x1C2126), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(p_reel->pack, LV_OPA_COVER, LV_PART_MAIN);

  p_reel->hub = lv_obj_create(parent);
  lv_obj_remove_style_all(p_reel->hub);
  lv_obj_set_size(p_reel->hub, REEL_HUB_R*2, REEL_HUB_R*2);
  lv_obj_align(p_reel->hub, LV_ALIGN_CENTER, cx, cy);
  lv_obj_set_style_radius(p_reel->hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(p_reel->hub, lv_color_hex(0xC9CED4), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(p_reel->hub, LV_OPA_COVER, LV_PART_MAIN);

  for (int i = 0; i < REEL_SPOKE_CNT; i++)
  {
    p_reel->spoke[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(p_reel->spoke[i]);
    lv_obj_set_size(p_reel->spoke[i], 8, 8);
    lv_obj_set_style_radius(p_reel->spoke[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p_reel->spoke[i], lv_color_hex(0x424A52), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_reel->spoke[i], LV_OPA_COVER, LV_PART_MAIN);
  }
}

void updateReel(reel_t *p_reel, int32_t angle, int32_t pack_r)
{
  lv_obj_set_size(p_reel->pack, pack_r*2, pack_r*2);
  lv_obj_align(p_reel->pack, LV_ALIGN_CENTER, p_reel->cx, p_reel->cy);

  for (int i = 0; i < REEL_SPOKE_CNT; i++)
  {
    int32_t a = angle + (360 / REEL_SPOKE_CNT) * i;
    int32_t x = (lv_trigo_cos(a) * (REEL_HUB_R - 5)) / LV_TRIGO_SIN_MAX;
    int32_t y = (lv_trigo_sin(a) * (REEL_HUB_R - 5)) / LV_TRIGO_SIN_MAX;

    lv_obj_align(p_reel->spoke[i], LV_ALIGN_CENTER, p_reel->cx + x, p_reel->cy + y);
  }
}

/* 선택된 테이프 이름을 스티커에 반영한다. */
void refreshTape(void)
{
  const char *name = cassetteAudioName(sel_idx);

  if (name != NULL)
    lv_label_set_text(label_title, name);
  else
    lv_label_set_text(label_title, "No Tape");
}

void btnPlayCb(lv_event_t *e)
{
  LV_UNUSED(e);
  if (cassetteAudioCount() > 0)
    cassetteAudioPlay(sel_idx);
}

void btnStopCb(lv_event_t *e)
{
  LV_UNUSED(e);
  cassetteAudioStop();
}

void btnPrevCb(lv_event_t *e)
{
  LV_UNUSED(e);
  if (cassetteAudioCount() > 0)
  {
    sel_idx = (sel_idx + cassetteAudioCount() - 1) % cassetteAudioCount();
    refreshTape();
  }
}

void btnNextCb(lv_event_t *e)
{
  LV_UNUSED(e);
  if (cassetteAudioCount() > 0)
  {
    sel_idx = (sel_idx + 1) % cassetteAudioCount();
    refreshTape();
  }
}

void btnRecCb(lv_event_t *e)
{
  LV_UNUSED(e);
  cassetteAudioRecord();
}




APP_DEF(cassette){
  .name   = "Cassette",
  .order  = 0,
  .init   = cassetteInit,
  .enter  = cassetteEnter,
  .update = cassetteUpdate,
  .exit   = cassetteExit,
};

#endif
