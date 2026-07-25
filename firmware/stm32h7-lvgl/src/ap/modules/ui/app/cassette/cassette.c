#include "cassette.h"
#include "cassette_audio.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

#define REEL_SPOKE_CNT      3
#define REEL_HUB_R          16
#define REEL_PACK_R_MIN     22
#define REEL_PACK_R_MAX     46
#define REEL_GAP            118         /* 좌우 릴 중심 간격의 절반 */

#define SPEC_BAR_W          7        /* 막대 폭 */
#define SPEC_BAR_GAP        3        /* 막대 간격 */
#define SPEC_MAX_H          64       /* 막대 최대 높이 (바닥→위, 한 방향) */

#define SPEC_SEG_H          6       /* 세그먼트(블럭) 높이 */
#define SPEC_SEG_GAP        3       /* 세그먼트 간격 */
#define SPEC_BG             0x0A0D10 /* 스펙트럼 배경(=테이프 창) 색 */

#define SPEC_CAP_H          3        /* 피크 홀드 캡 두께 */
#define SPEC_PEAK_FALL      2        /* 피크가 프레임당 내려오는 양 */
#define SPEC_BASE_COL       0x39434E /* 바닥 기준선 색 */

/* 막대는 아래(초록) → 위(빨강) 세로 그라디언트. 위로 갈수록(피크) 빨강. */
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
static uint32_t specLerp(uint32_t a, uint32_t b, int num, int den);  /* 색 선형보간 */
static void updateReel(reel_t *p_reel, int32_t angle, int32_t pack_r);
static void refreshTape(void);
static void btnPlayCb(lv_event_t *e);
static void btnStopCb(lv_event_t *e);
static void btnPrevCb(lv_event_t *e);
static void btnNextCb(lv_event_t *e);
static void btnRecCb(lv_event_t *e);
static void titleClickCb(lv_event_t *e);
static void fileItemCb(lv_event_t *e);
static void fileModalBgCb(lv_event_t *e);
static void openFileModal(void);
static void closeFileModal(void);


static lv_obj_t *cass_scr;      /* app 컨테이너 (모달 부모) */
static lv_obj_t *file_modal;    /* 파일 리스트 팝업 (열려 있으면 non-NULL) */

static reel_t    reel_l;
static reel_t    reel_r;
static lv_obj_t *label_title;
static lv_obj_t *label_state;
static lv_obj_t *label_time;
static lv_obj_t *bar_progress;
static lv_obj_t *spec_cover[CASS_SPECTRUM_BARS];  /* 상단 마스크(안 켜진 윗부분 가림) */
static lv_obj_t *spec_cap[CASS_SPECTRUM_BARS];    /* 피크 홀드 캡 */
static uint8_t   spec_peak[CASS_SPECTRUM_BARS];   /* 피크 값 (천천히 하강) */
static lv_obj_t *spec_base;                       /* 바닥 기준선 (재생 중에만 표시) */

static int32_t   reel_angle = 0;
static int32_t   reel_frac  = 0;   /* 회전 잔여각 누적 (deg*1000) */
static uint32_t  reel_last_ms = 0; /* 직전 갱신 시각 */
static int       sel_idx = 0;

/* 릴 회전 속도. 프레임레이트와 무관하게 시간 기반으로 돈다. */
#define REEL_DEG_PER_SEC   180




bool cassetteInit(void)
{
  return cassetteAudioInit();
}

bool cassetteEnter(lv_obj_t *scr)
{
  cassetteAudioScan();

  cass_scr   = scr;
  file_modal = NULL;

  createShell(scr);
  createTransport(scr);

  reel_angle   = 0;
  reel_frac    = 0;
  reel_last_ms = lv_tick_get();
  sel_idx      = 0;
  if (cassetteAudioGetIndex() >= 0)
    sel_idx = cassetteAudioGetIndex();

  refreshTape();
  return true;
}

void cassetteExit(void)
{
  file_modal = NULL;             /* 화면과 함께 삭제되므로 포인터만 정리 */
  cass_scr   = NULL;
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
  static int title_idx = -2;


  /* 재생/녹음 중이면 그 테이프 이름을, 아니면 선택한 이름을 보여준다. */
  {
    int show = (st != CASS_AUDIO_IDLE) ? cassetteAudioGetIndex() : sel_idx;
    if (show != title_idx)
    {
      const char *nm = cassetteAudioName(show);
      lv_label_set_text(label_title, nm != NULL ? nm : "No Tape");
      title_idx = show;
    }
  }


  /* 재생/녹음 중이면 릴을 돌린다. (시간 기반 — FPS 가 흔들려도 일정 속도) */
  if (st != CASS_AUDIO_IDLE)
  {
    uint32_t now = lv_tick_get();
    uint32_t dt  = now - reel_last_ms;

    reel_last_ms = now;
    if (dt > 200)                       /* 진입 직후 등 큰 지연은 제한 */
      dt = 200;

    /* 잔여각을 deg*1000 로 누적해 정수 절삭 손실을 없앤다. */
    reel_frac += (int32_t)dt * REEL_DEG_PER_SEC;
    if (reel_frac >= 1000)
    {
      reel_angle = (reel_angle + reel_frac / 1000) % 360;
      reel_frac %= 1000;
    }
  }
  else
  {
    reel_last_ms = lv_tick_get();       /* 정지 중엔 기준 시각만 갱신 */
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

  /* 바닥 기준선은 스펙트럼이 도는 재생/녹음 중에만 표시한다. */
  if (st != CASS_AUDIO_IDLE)
    lv_obj_remove_flag(spec_base, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(spec_base, LV_OBJ_FLAG_HIDDEN);

  /* 스펙트럼 : 바닥에서 위로 채워지는 블럭 막대(위로 갈수록 빨강) + 피크 홀드 캡.
   * 고정 그라디언트 위에 상단 마스크를 씌워 마스크를 줄이면 아래부터 드러난다. */
  cassetteAudioGetSpectrum(bars, CASS_SPECTRUM_BARS);
  for (int i = 0; i < CASS_SPECTRUM_BARS; i++)
  {
    int fill    = SPEC_MAX_H * bars[i] / 100;   /* 켜질 높이 */
    int cover_h = SPEC_MAX_H - fill;            /* 위에서 가릴 높이 */

    if (cover_h < 0)          cover_h = 0;
    if (cover_h > SPEC_MAX_H) cover_h = SPEC_MAX_H;
    lv_obj_set_height(spec_cover[i], cover_h);

    /* 피크 홀드 : 값이 오르면 즉시 따라가고, 아니면 천천히 내려온다. */
    if (bars[i] > spec_peak[i])
      spec_peak[i] = bars[i];
    else if (spec_peak[i] > SPEC_PEAK_FALL)
      spec_peak[i] -= SPEC_PEAK_FALL;
    else
      spec_peak[i] = 0;

    if (spec_peak[i] > 0)
    {
      int py = SPEC_MAX_H - (SPEC_MAX_H * spec_peak[i] / 100) - SPEC_CAP_H;
      if (py < 0) py = 0;
      lv_obj_set_y(spec_cap[i], py);
      /* 캡 색 = 그 높이의 그라디언트 색 (아래 초록 → 위 빨강 보간) */
      lv_obj_set_style_bg_color(spec_cap[i],
          lv_color_hex(specLerp(SPEC_COL_LO, SPEC_COL_HI, spec_peak[i], 100)), LV_PART_MAIN);
      lv_obj_remove_flag(spec_cap[i], LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
      lv_obj_add_flag(spec_cap[i], LV_OBJ_FLAG_HIDDEN);
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

  /* 스티커(제목 칸)를 누르면 파일 리스트 팝업 */
  lv_obj_add_flag(sticker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sticker, titleClickCb, LV_EVENT_CLICKED, NULL);

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

/* 두 24bit 색을 num/den 비율로 선형보간한다. (den 에서 b, 0 에서 a) */
uint32_t specLerp(uint32_t a, uint32_t b, int num, int den)
{
  int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  int r, g, bl;

  if (den <= 0)  den = 1;
  if (num < 0)   num = 0;
  if (num > den) num = den;

  r  = ar + (br - ar) * num / den;
  g  = ag + (bg - ag) * num / den;
  bl = ab + (bb - ab) * num / den;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* 스펙트럼을 세그먼트(블럭) 막대로 만든다. 바닥에서 위로 커진다.
 *  - 막대마다 고정 세로 그라디언트(아래 초록 → 위 빨강)를 깔고,
 *  - 그 위에 배경색 상단 마스크를 씌워 안 켜진 윗부분을 가린다(동적).
 *  - 전체 폭에 가로 간격 띠를 규칙적으로 얹어 블럭으로 잘라낸다(정적).
 *  - 맨 위에 피크 홀드 캡(밝은 막대)을 얹는다(동적).
 */
void createSpectrum(lv_obj_t *parent)
{
  int total_w = CASS_SPECTRUM_BARS * SPEC_BAR_W + (CASS_SPECTRUM_BARS - 1) * SPEC_BAR_GAP;
  int pitch   = SPEC_SEG_H + SPEC_SEG_GAP;
  lv_obj_t *cont;


  cont = lv_obj_create(parent);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, total_w, SPEC_MAX_H);
  lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < CASS_SPECTRUM_BARS; i++)
  {
    int       x = i * (SPEC_BAR_W + SPEC_BAR_GAP);
    lv_obj_t *grad;
    lv_obj_t *cover;

    /* 고정 그라디언트 막대 (아래 초록 → 위 빨강) */
    grad = lv_obj_create(cont);
    lv_obj_remove_style_all(grad);
    lv_obj_set_size(grad, SPEC_BAR_W, SPEC_MAX_H);
    lv_obj_align(grad, LV_ALIGN_BOTTOM_LEFT, x, 0);
    lv_obj_set_style_bg_color(grad, lv_color_hex(SPEC_COL_HI), LV_PART_MAIN);       /* 위 */
    lv_obj_set_style_bg_grad_color(grad, lv_color_hex(SPEC_COL_LO), LV_PART_MAIN);  /* 아래 */
    lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(grad, LV_OPA_COVER, LV_PART_MAIN);

    /* 상단 마스크 (동적) : 처음엔 전체를 가려 꺼진 상태 */
    cover = lv_obj_create(cont);
    lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover, SPEC_BAR_W, SPEC_MAX_H);
    lv_obj_align(cover, LV_ALIGN_TOP_LEFT, x, 0);
    lv_obj_set_style_bg_color(cover, lv_color_hex(SPEC_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);

    spec_cover[i] = cover;
  }

  /* 세그먼트 간격 띠 (정적) : 전체 폭에 가로로 깔아 막대를 블럭으로 자른다 */
  for (int y = SPEC_SEG_H; y < SPEC_MAX_H; y += pitch)
  {
    lv_obj_t *gap = lv_obj_create(cont);

    lv_obj_remove_style_all(gap);
    lv_obj_set_size(gap, total_w, SPEC_SEG_GAP);
    lv_obj_align(gap, LV_ALIGN_BOTTOM_LEFT, 0, -y);
    lv_obj_set_style_bg_color(gap, lv_color_hex(SPEC_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gap, LV_OPA_COVER, LV_PART_MAIN);
  }

  /* 피크 홀드 캡 (동적, 최상위) */
  for (int i = 0; i < CASS_SPECTRUM_BARS; i++)
  {
    int       x = i * (SPEC_BAR_W + SPEC_BAR_GAP);
    lv_obj_t *cap = lv_obj_create(cont);

    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, SPEC_BAR_W, SPEC_CAP_H);
    lv_obj_set_pos(cap, x, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(SPEC_COL_LO), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(cap, LV_OBJ_FLAG_HIDDEN);

    spec_cap[i]  = cap;
    spec_peak[i] = 0;
  }

  /* 바닥 기준선 : 재생 중 막대가 시작되는 바닥을 알 수 있게 얇게 깐다.
   * cont(막대 영역) 바로 아래에 두어 막대에 가리지 않는다. */
  {
    spec_base = lv_obj_create(parent);

    lv_obj_remove_style_all(spec_base);
    lv_obj_set_size(spec_base, total_w, 2);          /* 스펙트럼 막대 영역과 동일 폭 */
    lv_obj_align(spec_base, LV_ALIGN_CENTER, 0, SPEC_MAX_H / 2 + 3);
    lv_obj_set_style_radius(spec_base, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(spec_base, lv_color_hex(SPEC_BASE_COL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(spec_base, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(spec_base, LV_OBJ_FLAG_HIDDEN);   /* 재생 중에만 표시 */
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
  lv_obj_t *ring;

  p_reel->cx = cx;
  p_reel->cy = cy;

  /* 최대 테이프 감김 기준 원 (약하게). 테이프가 줄어도 최대 위치를 알 수 있다.
   * 맨 뒤에 두어 실제 테이프(pack)가 그 위에 그려진다. */
  ring = lv_obj_create(parent);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, REEL_PACK_R_MAX*2, REEL_PACK_R_MAX*2);
  lv_obj_align(ring, LV_ALIGN_CENTER, cx, cy);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(ring, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(ring, lv_color_hex(0x2E3742), LV_PART_MAIN);
  lv_obj_set_style_border_opa(ring, LV_OPA_60, LV_PART_MAIN);

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

/* 이전/다음 곡으로 이동한다. dir = -1(이전) / +1(다음).
 * 재생 중이면 곧바로 그 곡을 재생하고, 정지 상태면 곡 정보만 갱신한다.
 */
static void cassetteStep(int dir)
{
  int              cnt = cassetteAudioCount();
  int              base;
  CassAudioState_t st;

  if (cnt <= 0)
    return;

  st   = cassetteAudioGetState();
  base = sel_idx;

  /* 재생 중이면 실제로 재생 중인 곡을 기준으로 이동한다. */
  if (st != CASS_AUDIO_IDLE && cassetteAudioGetIndex() >= 0)
    base = cassetteAudioGetIndex();

  sel_idx = (base + dir + cnt) % cnt;

  if (st == CASS_AUDIO_PLAY)
    cassetteAudioPlay(sel_idx);   /* 재생 중 → 바로 다음/이전 곡 재생 */
  else
    refreshTape();                /* 정지 중 → 정보만 갱신 */
}

void btnPrevCb(lv_event_t *e)
{
  LV_UNUSED(e);
  cassetteStep(-1);
}

void btnNextCb(lv_event_t *e)
{
  LV_UNUSED(e);
  cassetteStep(+1);
}

void btnRecCb(lv_event_t *e)
{
  LV_UNUSED(e);
  cassetteAudioRecord();
}

/* 제목 칸을 누르면 파일 리스트 팝업 (토글) */
void titleClickCb(lv_event_t *e)
{
  LV_UNUSED(e);
  if (file_modal == NULL)
    openFileModal();
  else
    closeFileModal();
}

void closeFileModal(void)
{
  if (file_modal != NULL)
  {
    lv_obj_delete(file_modal);
    file_modal = NULL;
  }
}

void fileModalBgCb(lv_event_t *e)
{
  /* 배경(리스트 바깥)을 누르면 닫는다. */
  if (lv_event_get_target_obj(e) == file_modal)
    closeFileModal();
}

void fileItemCb(lv_event_t *e)
{
  lv_obj_t  *item = lv_event_get_target_obj(e);
  int        idx  = (int)(intptr_t)lv_event_get_user_data(e);
  lv_point_t p;
  lv_area_t  a;


  /* 릴리즈 지점이 눌렀던 항목 밖이면(드래그) 재생하지 않는다. */
  lv_indev_get_point(lv_indev_active(), &p);
  lv_obj_get_coords(item, &a);
  if (p.x < a.x1 || p.x > a.x2 || p.y < a.y1 || p.y > a.y2)
    return;

  sel_idx = idx;
  cassetteAudioPlay(idx);        /* SD 접근은 오디오 스레드가 단독 처리 */
  closeFileModal();
}

/* 캐시된 테이프 목록만 사용한다 (여기서 SD 를 다시 스캔하지 않는다). */
void openFileModal(void)
{
  lv_obj_t *panel;
  lv_obj_t *header;
  lv_obj_t *list;
  int       cnt = cassetteAudioCount();


  if (cass_scr == NULL)
    return;

  /* 반투명 배경 (탭하면 닫힘) */
  file_modal = lv_obj_create(cass_scr);
  lv_obj_remove_style_all(file_modal);
  lv_obj_set_size(file_modal, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(file_modal, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(file_modal, LV_OPA_60, LV_PART_MAIN);
  lv_obj_add_flag(file_modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(file_modal, fileModalBgCb, LV_EVENT_CLICKED, NULL);

  /* 패널 : 헤더(고정) + 리스트(스크롤) */
  panel = uiCreateCard(file_modal, LCD_WIDTH - UI_MARGIN*2, LCD_HEIGHT - UI_SPACE_XL*2);
  lv_obj_center(panel);
  lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

  /* --- 헤더 --- */
  header = lv_obj_create(panel);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_color(header, lv_color_hex(UI_COLOR_LINE), LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  {
    lv_obj_t *icon = uiCreateLabel(header, LV_SYMBOL_AUDIO, uiStyleTextBody());
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, UI_SPACE_MD, 0);
    lv_obj_t *t = uiCreateLabel(header, "Tapes", uiStyleTextBody());
    lv_obj_align(t, LV_ALIGN_LEFT_MID, UI_SPACE_MD + 40, 0);
    lv_obj_t *cntl = uiCreateLabel(header, "", uiStyleTextDim());
    lv_label_set_text_fmt(cntl, "%d", cnt);
    lv_obj_align(cntl, LV_ALIGN_RIGHT_MID, -UI_SPACE_MD, 0);
  }

  /* --- 스크롤되는 리스트 --- */
  list = lv_obj_create(panel);
  lv_obj_remove_style_all(list);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);              /* 남는 세로를 채운다 */
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);

  /* 우측 스크롤바 : 항상 표시 + 스타일 (remove_style_all 로 지워졌으므로 새로 준다) */
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_width(list, 6, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(list, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(list, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_70, LV_PART_SCROLLBAR);

  if (cnt == 0)
  {
    lv_obj_t *empty = uiCreateLabel(list, "No Tape", uiStyleTextDim());
    lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    return;
  }

  for (int i = 0; i < cnt; i++)
  {
    /* 리스트 항목 : 평평한 행 + 아래 구분선 */
    lv_obj_t *item = lv_obj_create(list);
    lv_obj_t *label;

    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), 60);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(item, lv_color_hex(UI_COLOR_SURFACE_ALT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(item, lv_color_hex(UI_COLOR_LINE), LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(item, fileItemCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    /* 현재 선택 표시 (점) 또는 자리맞춤 */
    if (i == sel_idx)
    {
      lv_obj_t *dot = lv_obj_create(item);
      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, 10, 10);
      lv_obj_align(dot, LV_ALIGN_LEFT_MID, UI_SPACE_MD, 0);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    }

    /* 파일 크기 (우측) */
    {
      uint32_t sz = cassetteAudioSize(i);
      lv_obj_t *szl = uiCreateLabel(item, "", uiStyleTextDim());

      if (sz >= 1024*1024)
        lv_label_set_text_fmt(szl, "%d.%d MB", (int)(sz/(1024*1024)), (int)((sz%(1024*1024))/(1024*105)));
      else
        lv_label_set_text_fmt(szl, "%d KB", (int)(sz/1024));
      lv_obj_align(szl, LV_ALIGN_RIGHT_MID, -UI_SPACE_MD, 0);
    }

    /* 파일명 (긴 경우 말줄임). 우측 크기 자리를 뺀 폭으로 제한. */
    label = uiCreateLabel(item, cassetteAudioName(i), uiStyleTextBody());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LCD_WIDTH - UI_MARGIN*2 - (UI_SPACE_MD + 24) - 110);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, UI_SPACE_MD + 24, 0);
  }
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
