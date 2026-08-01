/*
 * swdprog.c
 *
 *  SWD 오프라인 다운로더 앱
 *
 *  화면은 이미 CLI 로 끝까지 도는 prog_task 의 표현 계층이다. 여기서는 요청을
 *  남기고 상태를 폴링해 그린다 — 굽는 로직은 한 줄도 없다.
 *
 *  워커가 lv_* 를 부르지 않는 것과 짝으로, 여기서는 blocking 을 하지 않는다.
 *  SD 훑기(f_opendir/f_readdir)는 수십 ms 씩 멈추므로 목록도 워커가 만든다.
 */

#include "ui/app.h"
#include "ui/ui_theme.h"
#include "prog/prog_task.h"


#if defined(_USE_HW_LVGL) && defined(_USE_HW_SWD)




/* 세로 배치. 간격을 상수로 두고 카드 높이에서 다음 y 를 계산한다 —
   숫자를 흩뿌리면 하나만 고쳐도 간격이 어긋난다.

     상단 여백 16, 카드 사이 8, 카드와 버튼 사이도 8,
     좌우·아래 여백은 UI_MARGIN(24) 으로 같게. */
#define SWDPROG_GAP       UI_SPACE_SM
#define SWDPROG_BTN_H     56

/* 카드 안의 모든 줄을 이 격자에 올린다. 줄마다 y 를 손으로 적으면 20 과 24 가
   섞여 리듬이 깨진다 — 실제로 그랬다. */
#define SWDPROG_ROW       24
#define SWDPROG_PAD       UI_SPACE_SM
#define SWDPROG_LINE(n)   (SWDPROG_PAD + (n) * SWDPROG_ROW)

// 머리글 + 본문 + 부제 세 줄
#define SWDPROG_CARD_H    (SWDPROG_PAD * 2 + SWDPROG_ROW * 3)

#define SWDPROG_CARD_Y0   56
#define SWDPROG_CARD_Y1   (SWDPROG_CARD_Y0 + SWDPROG_CARD_H + SWDPROG_GAP)
#define SWDPROG_CARD_Y2   (SWDPROG_CARD_Y1 + SWDPROG_CARD_H + SWDPROG_GAP)

/* 진행 카드는 버튼 위까지 채우고, 남는 높이에 들어가는 만큼만 로그를 둔다.
   줄 수를 손으로 적으면 높이를 바꿀 때마다 어긋난다. */
#define SWDPROG_PROG_H    (480 - UI_MARGIN - SWDPROG_BTN_H - SWDPROG_GAP - SWDPROG_CARD_Y2)
#define SWDPROG_LOG_ROWS  ((SWDPROG_PROG_H - SWDPROG_PAD * 2 - SWDPROG_ROW * 2) / SWDPROG_ROW)


static bool swdprogInit(void);
static bool swdprogEnter(lv_obj_t *scr);
static void swdprogUpdate(void);
static void swdprogExit(void);

static void swdprogBuildCards(lv_obj_t *parent);
static void swdprogSetTargetText(void);
static void swdprogSetProjText(void);
static void swdprogSetButtons(void);
static void swdprogScanCb(lv_event_t *e);
static void swdprogPickCb(lv_event_t *e);
static void swdprogStartCb(lv_event_t *e);
static void swdprogItemCb(lv_event_t *e);
static void swdprogModalBgCb(lv_event_t *e);
static void swdprogOpenModal(void);
static void swdprogCloseModal(void);


static lv_obj_t *swd_scr;
static lv_obj_t *modal;

static lv_obj_t *lbl_chip;        // 우상단 칩 이름
static lv_obj_t *dot_link;
static lv_obj_t *lbl_target;
static lv_obj_t *lbl_target_sub;
static lv_obj_t *lbl_proj;
static lv_obj_t *lbl_proj_sub;
static lv_obj_t *lbl_phase;
static lv_obj_t *lbl_pct;
static lv_obj_t *bar_prog;
static lv_obj_t *lbl_log[SWDPROG_LOG_ROWS];
static lv_obj_t *btn_start;
static lv_obj_t *lbl_start;
static lv_obj_t *btn_scan;

static char      sel_proj[JOB_PROJ_MAX];
static char      sel_name[JOB_NAME_MAX];
static uint8_t   last_log_seq = 0xFF;
static uint8_t   last_pct     = 0xFF;
static prog_state_t last_state = PROG_IDLE;


APP_DEF(swdprog){
  .name   = "SWD Programmer",
  .order  = 5,
  .init   = swdprogInit,
  .enter  = swdprogEnter,
  .update = swdprogUpdate,
  .exit   = swdprogExit,
};


// ----------------------------------------------------------------- 초기화

static bool swdprogInit(void)
{
  return true;      // 워커는 prog 모듈이 띄운다
}


// ----------------------------------------------------------------- 화면

static bool swdprogEnter(lv_obj_t *scr)
{
  lv_obj_t *title;

  swd_scr = scr;
  modal   = NULL;

  last_log_seq = 0xFF;
  last_pct     = 0xFF;
  last_state   = PROG_IDLE;

  title = uiCreateLabel(scr, "SWD Programmer", uiStyleTextBody());
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, UI_MARGIN, UI_SPACE_MD);

  lbl_chip = uiCreateLabel(scr, "-", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_chip, uiFontCaption(), 0);
  lv_obj_align(lbl_chip, LV_ALIGN_TOP_RIGHT, -UI_MARGIN - 20, UI_SPACE_MD + 5);

  dot_link = lv_obj_create(scr);
  lv_obj_remove_style_all(dot_link);
  lv_obj_set_size(dot_link, 12, 12);
  lv_obj_set_style_radius(dot_link, 6, 0);
  lv_obj_set_style_bg_opa(dot_link, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
  lv_obj_align(dot_link, LV_ALIGN_TOP_RIGHT, -UI_MARGIN, UI_SPACE_MD + 6);

  swdprogBuildCards(scr);

  // 들어오면 프로젝트 목록부터 훑어둔다 (모달은 캐시만 그린다)
  progTaskList();

  swdprogSetTargetText();
  swdprogSetProjText();
  swdprogSetButtons();
  return true;
}

static void swdprogBuildCards(lv_obj_t *parent)
{
  lv_obj_t *card;
  lv_obj_t *hint;
  int32_t   w = lv_obj_get_width(parent) - UI_MARGIN * 2;

  if (w <= 0) w = 480 - UI_MARGIN * 2;

  // ---- 타깃
  card = uiCreateCard(parent, w, SWDPROG_CARD_H);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, SWDPROG_CARD_Y0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, swdprogScanCb, LV_EVENT_CLICKED, NULL);

  hint = uiCreateLabel(card, "TARGET", uiStyleTextDim());
  lv_obj_set_style_text_font(hint, uiFontCaption(), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(0));
  hint = uiCreateLabel(card, "SCAN >", uiStyleTextDim());
  lv_obj_set_style_text_font(hint, uiFontCaption(), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -UI_SPACE_MD, SWDPROG_LINE(0));

  lbl_target = uiCreateLabel(card, "-", uiStyleTextBody());
  lv_obj_set_width(lbl_target, w - UI_SPACE_MD * 2);
  lv_label_set_long_mode(lbl_target, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(lbl_target, uiFontCaption(), 0);
  lv_obj_align(lbl_target, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(1));
  lbl_target_sub = uiCreateLabel(card, "탭하면 찾는다", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_target_sub, uiFontCaption(), 0);
  lv_obj_set_width(lbl_target_sub, w - UI_SPACE_MD * 2);
  lv_label_set_long_mode(lbl_target_sub, LV_LABEL_LONG_DOT);
  lv_obj_align(lbl_target_sub, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(2));

  // ---- 펌웨어
  card = uiCreateCard(parent, w, SWDPROG_CARD_H);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, SWDPROG_CARD_Y1);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, swdprogPickCb, LV_EVENT_CLICKED, NULL);

  hint = uiCreateLabel(card, "FIRMWARE", uiStyleTextDim());
  lv_obj_set_style_text_font(hint, uiFontCaption(), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(0));
  hint = uiCreateLabel(card, ">", uiStyleTextDim());
  lv_obj_set_style_text_font(hint, uiFontCaption(), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -UI_SPACE_MD, SWDPROG_LINE(0));

  lbl_proj = uiCreateLabel(card, "-", uiStyleTextBody());
  lv_obj_set_width(lbl_proj, w - UI_SPACE_MD * 2);
  lv_label_set_long_mode(lbl_proj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(lbl_proj, uiFontCaption(), 0);
  lv_obj_align(lbl_proj, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(1));
  lbl_proj_sub = uiCreateLabel(card, "/prog/fw", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_proj_sub, uiFontCaption(), 0);
  lv_obj_set_width(lbl_proj_sub, w - UI_SPACE_MD * 2);
  lv_label_set_long_mode(lbl_proj_sub, LV_LABEL_LONG_DOT);
  lv_obj_align(lbl_proj_sub, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(2));

  // ---- 진행
  card = uiCreateCard(parent, w, SWDPROG_PROG_H);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, SWDPROG_CARD_Y2);

  lbl_phase = uiCreateLabel(card, "READY", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_phase, uiFontCaption(), 0);
  lv_obj_align(lbl_phase, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(0));
  lbl_pct = uiCreateLabel(card, "", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_pct, uiFontCaption(), 0);
  lv_obj_align(lbl_pct, LV_ALIGN_TOP_RIGHT, -UI_SPACE_MD, SWDPROG_LINE(0));

  bar_prog = lv_bar_create(card);
  lv_obj_set_size(bar_prog, w - UI_SPACE_MD * 2, 10);
  lv_obj_align(bar_prog, LV_ALIGN_TOP_LEFT, UI_SPACE_MD,
               SWDPROG_LINE(1) + (SWDPROG_ROW - 10) / 2);
  lv_obj_set_style_bg_color(bar_prog, lv_color_hex(UI_COLOR_SURFACE_ALT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_prog, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar_prog, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(bar_prog, 5, LV_PART_INDICATOR);
  lv_bar_set_value(bar_prog, 0, LV_ANIM_OFF);

  for (int i = 0; i < SWDPROG_LOG_ROWS; i++)
  {
    lbl_log[i] = uiCreateLabel(card, "", uiStyleTextDim());
    lv_obj_set_style_text_font(lbl_log[i], uiFontCaption(), 0);
    lv_obj_set_width(lbl_log[i], w - UI_SPACE_MD * 2);
    lv_label_set_long_mode(lbl_log[i], LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_log[i], LV_ALIGN_TOP_LEFT, UI_SPACE_MD, SWDPROG_LINE(2 + i));
  }

  // ---- 버튼
  btn_start = uiCreateButton(parent, "START", true);
  lv_obj_set_size(btn_start, (w - SWDPROG_GAP) / 2, SWDPROG_BTN_H);
  lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
  lv_obj_add_event_cb(btn_start, swdprogStartCb, LV_EVENT_CLICKED, NULL);
  lbl_start = lv_obj_get_child(btn_start, 0);

  btn_scan = uiCreateButton(parent, "SCAN", false);
  lv_obj_set_size(btn_scan, (w - SWDPROG_GAP) / 2, SWDPROG_BTN_H);
  lv_obj_align(btn_scan, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
  lv_obj_add_event_cb(btn_scan, swdprogScanCb, LV_EVENT_CLICKED, NULL);
}

/* 화면 객체는 런처가 지운다. 여기서는 포인터만 버린다 — 워커는 계속 돌게 둬서
   나갔다 들어와도 진행 중인 잡에 다시 붙는다. */
static void swdprogExit(void)
{
  swd_scr  = NULL;
  modal    = NULL;
  btn_start = NULL;
}


// ----------------------------------------------------------------- 갱신

static void swdprogUpdate(void)
{
  prog_state_t st;
  uint8_t      pct;
  uint8_t      seq;

  if (swd_scr == NULL) return;

  st  = progTaskGetState();
  pct = progTaskGetPercent();
  seq = progTaskGetLogSeq();

  if (pct != last_pct)
  {
    lv_bar_set_value(bar_prog, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(lbl_pct, "%d %%", (int)pct);
    last_pct = pct;
  }

  if (st != last_state)
  {
    const char *txt = "READY";

    switch (st)
    {
      case PROG_SCANNING: txt = "SCANNING";  break;
      case PROG_LISTING:  txt = "LOADING";   break;
      case PROG_RUNNING:  txt = "RUNNING";   break;
      case PROG_DONE:     txt = "DONE";      break;
      case PROG_ERROR:    txt = "ERROR";     break;
      default: break;
    }
    lv_label_set_text(lbl_phase, txt);
    lv_obj_set_style_text_color(lbl_phase,
        lv_color_hex((st == PROG_ERROR) ? UI_COLOR_ACCENT :
                     (st == PROG_DONE)  ? UI_COLOR_OK : UI_COLOR_TEXT_DIM), 0);

    swdprogSetTargetText();
    swdprogSetButtons();
    last_state = st;
  }
  else if (st == PROG_RUNNING)
  {
    lv_label_set_text(lbl_phase, progTaskGetPhase());
  }

  if (seq != last_log_seq)
  {
    uint32_t cnt   = progTaskGetLogCnt();
    uint32_t first = (cnt > SWDPROG_LOG_ROWS) ? cnt - SWDPROG_LOG_ROWS : 0;

    for (int i = 0; i < SWDPROG_LOG_ROWS; i++)
    {
      uint32_t idx = first + (uint32_t)i;

      lv_label_set_text(lbl_log[i], (idx < cnt) ? progTaskGetLog(idx) : "");
    }
    last_log_seq = seq;
  }
}

static void swdprogSetTargetText(void)
{
  const prog_target_t *t = progTaskGetTarget();

  if (lbl_target == NULL) return;

  if (t->is_valid == false)
  {
    lv_label_set_text(lbl_target, "-");
    lv_label_set_text(lbl_target_sub, "탭하면 찾는다");
    lv_label_set_text(lbl_chip, "-");
    lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    return;
  }

  lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_OK), 0);

  if (t->dev_found)
  {
    lv_label_set_text(lbl_target, t->dev.name);
    lv_label_set_text(lbl_chip, t->dev.name);
    lv_label_set_text_fmt(lbl_target_sub, "%s  ap %d  ram 0x%08X",
                          t->dev.cpu[0] ? t->dev.cpu : "?", (int)t->ap,
                          (unsigned int)t->dev.ram);
  }
  else
  {
    lv_label_set_text(lbl_target, "알 수 없는 MCU");
    lv_label_set_text(lbl_chip, "?");
    lv_label_set_text_fmt(lbl_target_sub, "id 0x%08X  cpuid 0x%08X",
                          (unsigned int)t->id_read, (unsigned int)t->cpuid);
  }
}

static void swdprogSetProjText(void)
{
  if (lbl_proj == NULL) return;

  if (sel_proj[0] == 0)
  {
    lv_label_set_text(lbl_proj, "-");
    lv_label_set_text(lbl_proj_sub, "/prog/fw");
  }
  else
  {
    lv_label_set_text(lbl_proj, sel_name);
    lv_label_set_text(lbl_proj_sub, sel_proj);
  }
}

/* 굽는 동안 START 는 ABORT 가 되고 선택 카드는 못 누르게 한다. */
static void swdprogSetButtons(void)
{
  bool busy = (progTaskGetState() == PROG_RUNNING);

  if (btn_start == NULL) return;

  lv_label_set_text(lbl_start, busy ? "ABORT" : "START");

  if (busy || sel_proj[0] == 0) lv_obj_add_state(btn_start, LV_STATE_DISABLED);
  else                          lv_obj_remove_state(btn_start, LV_STATE_DISABLED);
  if (busy) lv_obj_remove_state(btn_start, LV_STATE_DISABLED);   // ABORT 는 눌려야 한다

  if (busy) lv_obj_add_state(btn_scan, LV_STATE_DISABLED);
  else      lv_obj_remove_state(btn_scan, LV_STATE_DISABLED);
}


// ----------------------------------------------------------------- 이벤트

static void swdprogScanCb(lv_event_t *e)
{
  (void)e;

  if (progTaskGetState() == PROG_RUNNING) return;

  progTaskScan();
  last_state = PROG_IDLE;      // 다음 update 에서 다시 그리게 한다
}

static void swdprogPickCb(lv_event_t *e)
{
  (void)e;

  if (progTaskGetState() == PROG_RUNNING) return;

  if (modal == NULL) swdprogOpenModal();
  else               swdprogCloseModal();
}

static void swdprogStartCb(lv_event_t *e)
{
  (void)e;

  if (progTaskGetState() == PROG_RUNNING)
  {
    progTaskAbort();
    return;
  }
  if (sel_proj[0] == 0) return;

  progTaskRun(sel_proj);
  last_state = PROG_IDLE;
}


// ----------------------------------------------------------------- 모달

static void swdprogOpenModal(void)
{
  lv_obj_t *panel;
  lv_obj_t *header;
  lv_obj_t *list;
  uint32_t  cnt = progTaskGetProjCnt();

  modal = lv_obj_create(swd_scr);
  lv_obj_remove_style_all(modal);
  lv_obj_set_size(modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(modal, LV_OPA_60, 0);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(modal, swdprogModalBgCb, LV_EVENT_CLICKED, NULL);

  panel = uiCreateCard(modal, 400, 340);
  lv_obj_center(panel);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);   // 배경 클릭이 새지 않게

  header = uiCreateLabel(panel, "FIRMWARE", uiStyleTextDim());
  lv_obj_set_style_pad_left(header, UI_SPACE_MD, 0);
  lv_obj_set_style_pad_top(header, UI_SPACE_MD, 0);

  list = lv_obj_create(panel);
  lv_obj_remove_style_all(list);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, UI_SPACE_SM, 0);

  if (cnt == 0)
  {
    uiCreateLabel(list, "fw.txt 가 없다", uiStyleTextDim());
  }

  for (uint32_t i = 0; i < cnt; i++)
  {
    const prog_proj_t *p = progTaskGetProj(i);
    lv_obj_t          *row;

    if (p == NULL) break;

    row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), UI_TOUCH_MIN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, swdprogItemCb, LV_EVENT_RELEASED, (void *)(uintptr_t)i);

    {
      lv_obj_t *l = uiCreateLabel(row, p->name, uiStyleTextBody());

      lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_SPACE_SM, -10);
      l = uiCreateLabel(row, p->proj, uiStyleTextDim());
      lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_SPACE_SM, 12);
    }
  }
}

static void swdprogCloseModal(void)
{
  if (modal != NULL)
  {
    lv_obj_del(modal);
    modal = NULL;
  }
}

static void swdprogModalBgCb(lv_event_t *e)
{
  if (lv_event_get_target(e) == modal) swdprogCloseModal();
}

/* 스크롤 플릭이 선택으로 새지 않게 릴리즈 좌표가 행 안인지 본다.
   여기서 잘못 고르면 엉뚱한 펌웨어를 굽는다. */
static void swdprogItemCb(lv_event_t *e)
{
  lv_obj_t          *row = lv_event_get_target(e);
  uint32_t           idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  const prog_proj_t *p;
  lv_point_t         pt;
  lv_area_t          area;

  lv_indev_get_point(lv_indev_active(), &pt);
  lv_obj_get_coords(row, &area);
  if (pt.x < area.x1 || pt.x > area.x2 || pt.y < area.y1 || pt.y > area.y2) return;

  p = progTaskGetProj(idx);
  if (p == NULL) return;

  snprintf(sel_proj, sizeof(sel_proj), "%s", p->proj);
  snprintf(sel_name, sizeof(sel_name), "%s", p->name);

  swdprogCloseModal();
  swdprogSetProjText();
  swdprogSetButtons();
}


#endif
