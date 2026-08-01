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
 *
 *  화면을 넷으로 나눈다. 480x480 한 장에 설정과 실행을 같이 두면 굽는 중에도
 *  선택 카드가 자리를 차지하는데, 그때 보고 싶은 건 진행률과 단계다. 반대로
 *  고를 때는 진행 카드가 빈 채로 자리만 먹는다.
 *
 *    HOME  타깃 요약 · 펌웨어 요약 · 큰 START
 *    LIST  펌웨어 목록 (전체 화면 스크롤)
 *    RUN   큰 퍼센트 · 단계 목록 (스크롤) · ABORT
 *    CFG   설정
 *
 *  실행 화면을 나누면 퍼센트를 크게 쓸 수 있고, 굽는 중에는 선택 UI 가 아예
 *  없으니 오조작이 구조적으로 막힌다. 지금까지는 LV_STATE_DISABLED 로 막았는데
 *  그건 규칙이지 구조가 아니다.
 */

#include "ui/app.h"
#include "ui/ui_theme.h"
#include "prog/prog_task.h"


#if defined(_USE_HW_LVGL) && defined(_USE_HW_SWD)


/* 줄 격자. 카드 안의 모든 줄을 여기 올린다 — 줄마다 y 를 손으로 적으면 20 과
   24 가 섞여 리듬이 깨진다. */
#define ROW           24
#define PAD           UI_SPACE_SM
#define LINE(n)       (PAD + (n) * ROW)
#define GAP           UI_SPACE_SM
#define BTN_H         60
#define CARD_H        (PAD * 2 + ROW * 3)

/* 화면 맨 위 48px 은 상단 셰이드의 센서(lv_layer_top)가 덮고 있다. 여기에
   누를 것을 두면 터치가 셰이드로 가서 반응하지 않는다. 제목처럼 안 누르는
   것만 올린다.

   누를 수 있는 것은 전부 HEAD_H 아래에서 시작한다 — 두 값을 따로 적으면
   나중에 하나만 바꿨을 때 조용히 안 눌리는 버튼이 생긴다. */
#define SHADE_H       48
#define HEAD_H        (SHADE_H + 12)


typedef enum
{
  PAGE_HOME = 0,
  PAGE_LIST,
  PAGE_RUN,
  PAGE_CFG,
  PAGE_CNT,
} page_t;


static bool swdprogInit(void);
static bool swdprogEnter(lv_obj_t *scr);
static void swdprogUpdate(void);
static void swdprogExit(void);

static void      pageShow(page_t page);
static void      buildHome(lv_obj_t *parent);
static void      buildList(lv_obj_t *parent);
static void      buildRun(lv_obj_t *parent);
static void      buildCfg(lv_obj_t *parent);
static void      homeRefresh(void);
static void      listRefresh(void);
static void      runRefresh(bool rebuild);
static void      cfgRefresh(void);
static lv_obj_t *makeCard(lv_obj_t *parent, int32_t h, const char *hint, const char *right);
static lv_obj_t *makeRowLabel(lv_obj_t *card, int n, lv_style_t *st);
static lv_obj_t *makeHeader(lv_obj_t *parent, const char *text);

static void cbScan(lv_event_t *e);
static void cbPick(lv_event_t *e);
static void cbStart(lv_event_t *e);
static void cbCfgOpen(lv_event_t *e);
static void cbBack(lv_event_t *e);
static void cbItem(lv_event_t *e);
static void cbOpt(lv_event_t *e);
static void cbRunScroll(lv_event_t *e);


static lv_obj_t *page[PAGE_CNT];
static page_t    cur_page;
static int32_t   body_w;

// HOME
static lv_obj_t *lbl_chip;
static lv_obj_t *dot_link;
static lv_obj_t *lbl_tgt;
static lv_obj_t *lbl_tgt_sub;
static lv_obj_t *lbl_fw;
static lv_obj_t *lbl_fw_sub;
static lv_obj_t *lbl_count;
static lv_obj_t *btn_start;

// LIST
static lv_obj_t *list_box;

// RUN
static lv_obj_t *lbl_big;
static lv_obj_t *lbl_phase;
static lv_obj_t *bar_prog;
static lv_obj_t *step_box;
static lv_obj_t *lbl_result;
static lv_obj_t *lbl_abort;
static uint32_t  step_drawn;
static bool      step_follow = true;

// CFG
static lv_obj_t *lbl_opt[4];

static char         sel_proj[JOB_PROJ_MAX];
static char         sel_name[JOB_NAME_MAX];
static uint8_t      last_step_seq = 0xFF;
static uint8_t      last_pct      = 0xFF;
/* 워커 기동을 슬라이드가 끝난 뒤로 미룬다. enter() 는 런처가 화면을 밀어 넣기
   전에 부르는데, 거기서 스캔을 걸면 SWD 비트뱅잉(DWT 로 busy-wait 한다)과
   SD 훑기가 애니메이션과 같이 돌아 프레임을 떨어뜨린다. */
static uint32_t     enter_ms;
static bool         kicked;
static uint8_t      last_proj_seq = 0xFF;
static prog_state_t last_state    = PROG_STATE_CNT;


APP_DEF(swdprog){
  .name   = "Programmer",
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
  body_w = lv_obj_get_width(scr) - UI_MARGIN * 2;
  if (body_w <= 0) body_w = 480 - UI_MARGIN * 2;

  last_step_seq = 0xFF;
  last_pct      = 0xFF;
  last_proj_seq = 0xFF;
  last_state    = PROG_STATE_CNT;
  step_drawn    = 0;
  step_follow   = true;

  /* 마지막에 고른 펌웨어를 되살린다. 같은 펌웨어를 여러 보드에 반복해 굽는
     쓰임이라, 전원을 껐다 켤 때마다 다시 고르게 하면 안 된다. */
  snprintf(sel_proj, sizeof(sel_proj), "%s", progTaskGetProject());
  sel_name[0] = 0;

  for (int i = 0; i < PAGE_CNT; i++)
  {
    page[i] = lv_obj_create(scr);
    lv_obj_remove_style_all(page[i]);
    lv_obj_set_size(page[i], LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(page[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page[i], LV_OBJ_FLAG_HIDDEN);
  }

  buildHome(page[PAGE_HOME]);
  buildList(page[PAGE_LIST]);
  buildRun(page[PAGE_RUN]);
  buildCfg(page[PAGE_CFG]);

  progTaskList();          // 목록은 워커가 만든다 (SD 훑기는 수십 ms 씩 멈춘다)
  progTaskScan();          // 들어오면 바로 타깃을 본다

  pageShow(PAGE_HOME);
  homeRefresh();
  cfgRefresh();
  return true;
}

/* 화면 객체는 런처가 지운다. 포인터만 버리고 워커는 계속 돌게 둔다 —
   나갔다 들어와도 진행 중인 잡에 다시 붙는다. */
static void swdprogExit(void)
{
  for (int i = 0; i < PAGE_CNT; i++) page[i] = NULL;
  btn_start = NULL;
  step_box  = NULL;
  list_box  = NULL;
  lbl_opt[0] = NULL;
}

static void pageShow(page_t p)
{
  for (int i = 0; i < PAGE_CNT; i++)
  {
    if (page[i] == NULL) continue;

    if (i == (int)p) lv_obj_clear_flag(page[i], LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(page[i], LV_OBJ_FLAG_HIDDEN);
  }
  cur_page = p;
}


// ----------------------------------------------------------------- 공통 위젯

static lv_obj_t *makeCard(lv_obj_t *parent, int32_t h, const char *hint, const char *right)
{
  lv_obj_t *card = uiCreateCard(parent, body_w, h);
  lv_obj_t *l;

  /* 카드 기본 패딩이 내용 영역을 줄여 아래쪽 글자를 자른다. 여기서는 좌표로
     직접 배치하므로 패딩을 없앤다. */
  lv_obj_set_style_pad_all(card, 0, 0);

  if (hint != NULL)
  {
    l = uiCreateLabel(card, hint, uiStyleTextDim());
    lv_obj_set_style_text_font(l, uiFontCaption(), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, LINE(0));
  }
  if (right != NULL)
  {
    l = uiCreateLabel(card, right, uiStyleTextDim());
    lv_obj_set_style_text_font(l, uiFontCaption(), 0);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -UI_SPACE_MD, LINE(0));
  }
  return card;
}

static lv_obj_t *makeRowLabel(lv_obj_t *card, int n, lv_style_t *st)
{
  lv_obj_t *l = uiCreateLabel(card, "", st);

  lv_obj_set_style_text_font(l, uiFontCaption(), 0);
  lv_obj_set_width(l, body_w - UI_SPACE_MD * 2);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, LINE(n));
  return l;
}

/* 제목은 위에, 닫기는 아래에 둔다.

   위쪽 48px 은 셰이드 센서가 먹으므로 거기 버튼을 두면 안 눌린다. 아래에 두면
   HOME 의 START 와 같은 자리라 손가락이 가는 곳도 일정하다. */
static lv_obj_t *makeHeader(lv_obj_t *parent, const char *text)
{
  lv_obj_t *l = uiCreateLabel(parent, text, uiStyleTextBody());
  lv_obj_t *close;

  lv_obj_align(l, LV_ALIGN_TOP_LEFT, UI_MARGIN, UI_SPACE_MD);

  close = uiCreateButton(parent, "닫기", false);
  lv_obj_set_size(close, body_w, BTN_H);
  lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -UI_MARGIN);
  lv_obj_add_event_cb(close, cbBack, LV_EVENT_CLICKED, NULL);
  return l;
}


// ----------------------------------------------------------------- HOME

static void buildHome(lv_obj_t *parent)
{
  lv_obj_t *title;
  lv_obj_t *card;
  lv_obj_t *btn;
  int32_t   y = HEAD_H;

  title = uiCreateLabel(parent, "PROGRAMMER", uiStyleTextBody());
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, UI_MARGIN, UI_SPACE_MD);

  lbl_chip = uiCreateLabel(parent, "-", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_chip, uiFontCaption(), 0);
  lv_obj_align(lbl_chip, LV_ALIGN_TOP_RIGHT, -UI_MARGIN - 20, UI_SPACE_MD + 4);

  dot_link = lv_obj_create(parent);
  lv_obj_remove_style_all(dot_link);
  lv_obj_set_size(dot_link, 12, 12);
  lv_obj_set_style_radius(dot_link, 6, 0);
  lv_obj_set_style_bg_opa(dot_link, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
  lv_obj_align(dot_link, LV_ALIGN_TOP_RIGHT, -UI_MARGIN, UI_SPACE_MD + 8);

  card = makeCard(parent, CARD_H, "TARGET", "SCAN >");
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, cbScan, LV_EVENT_CLICKED, NULL);
  lbl_tgt     = makeRowLabel(card, 1, uiStyleTextBody());
  lbl_tgt_sub = makeRowLabel(card, 2, uiStyleTextDim());

  y += CARD_H + GAP;
  card = makeCard(parent, CARD_H, "FIRMWARE", ">");
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, cbPick, LV_EVENT_CLICKED, NULL);
  lbl_fw     = makeRowLabel(card, 1, uiStyleTextBody());
  lbl_fw_sub = makeRowLabel(card, 2, uiStyleTextDim());

  // 몇 장 했는지 센다. 여러 보드에 반복하는 작업에서 실제로 쓸모 있다.
  lbl_count = uiCreateLabel(parent, "", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_count, uiFontCaption(), 0);
  lv_obj_align(lbl_count, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -(BTN_H + UI_MARGIN + GAP + 4));

  btn = uiCreateButton(parent, LV_SYMBOL_SETTINGS, false);
  lv_obj_set_size(btn, 60, 44);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -(BTN_H + UI_MARGIN + GAP));
  lv_obj_add_event_cb(btn, cbCfgOpen, LV_EVENT_CLICKED, NULL);

  /* 버튼 하나로 시작한다. 상용 오프라인 다운로더가 그렇게 한다 — 현장에서는
     "확인하고 누르기" 만 남아야 하고, 그래야 버튼을 크게 만들 수 있다. */
  btn_start = uiCreateButton(parent, "START", true);
  lv_obj_set_size(btn_start, body_w, BTN_H);
  lv_obj_align(btn_start, LV_ALIGN_BOTTOM_MID, 0, -UI_MARGIN);
  lv_obj_add_event_cb(btn_start, cbStart, LV_EVENT_CLICKED, NULL);
}

static void homeRefresh(void)
{
  const prog_target_t *t = progTaskGetTarget();

  if (lbl_tgt == NULL) return;

  if (t->is_valid == false)
  {
    lv_label_set_text(lbl_tgt, "-");
    lv_label_set_text(lbl_tgt_sub, "탭하면 찾는다");
    lv_label_set_text(lbl_chip, "-");
    lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
  }
  else if (t->dev_found)
  {
    lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_OK), 0);
    lv_label_set_text(lbl_tgt, t->dev.name);
    lv_label_set_text(lbl_chip, t->dev.name);
    lv_label_set_text_fmt(lbl_tgt_sub, "%s  ap %d  ram 0x%08X",
                          t->dev.cpu[0] ? t->dev.cpu : "?", (int)t->ap,
                          (unsigned int)t->dev.ram);
  }
  else
  {
    lv_obj_set_style_bg_color(dot_link, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_label_set_text(lbl_tgt, "알 수 없는 MCU");
    lv_label_set_text(lbl_chip, "?");
    lv_label_set_text_fmt(lbl_tgt_sub, "id 0x%08X", (unsigned int)t->id_read);
  }

  if (sel_proj[0] == 0)
  {
    lv_label_set_text(lbl_fw, "-");
    lv_label_set_text(lbl_fw_sub, "탭해서 고른다");
  }
  else
  {
    lv_label_set_text(lbl_fw, sel_name[0] ? sel_name : sel_proj);
    lv_label_set_text(lbl_fw_sub, sel_proj);
  }

  /* 펌웨어를 골랐고 타깃이 붙어 있어야 시작할 수 있다. 둘 중 하나라도 없으면
     눌러봐야 실패하므로 아예 못 누르게 한다.

     버튼 글자는 언제나 START 다. 비활성일 때 "펌웨어 선택" 같은 걸 적으면
     누르면 선택 화면이 열릴 것처럼 보인다 - 상태를 동작처럼 쓰면 안 된다.
     이유는 버튼 위 한 줄에 적는다. */
  {
    const char *why = NULL;

    if (t->is_valid == false)   why = "타깃이 없다";
    else if (sel_proj[0] == 0)  why = "펌웨어를 고른다";

    if (why != NULL)
    {
      lv_obj_add_state(btn_start, LV_STATE_DISABLED);
      lv_label_set_text(lbl_count, why);
      lv_obj_set_style_text_color(lbl_count, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    }
    else
    {
      lv_obj_remove_state(btn_start, LV_STATE_DISABLED);
      lv_obj_set_style_text_color(lbl_count, lv_color_hex(UI_COLOR_OK), 0);

      if (progTaskGetOkCount() > 0)
        lv_label_set_text_fmt(lbl_count, "%d 장 완료", (int)progTaskGetOkCount());
      else
        lv_label_set_text(lbl_count, "");
    }
  }
}


// ----------------------------------------------------------------- LIST

static void buildList(lv_obj_t *parent)
{
  makeHeader(parent, "FIRMWARE");

  list_box = lv_obj_create(parent);
  lv_obj_remove_style_all(list_box);
  lv_obj_set_size(list_box, body_w, 480 - HEAD_H - BTN_H - UI_MARGIN - GAP);
  lv_obj_align(list_box, LV_ALIGN_TOP_MID, 0, HEAD_H);
  lv_obj_set_flex_flow(list_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list_box, GAP, 0);
  lv_obj_set_scroll_dir(list_box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list_box, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(list_box, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list_box, LV_OPA_50, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list_box, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list_box, 2, LV_PART_SCROLLBAR);
}

static void listRefresh(void)
{
  uint32_t cnt = progTaskGetProjCnt();

  if (list_box == NULL) return;

  lv_obj_clean(list_box);

  if (cnt == 0)
  {
    uiCreateLabel(list_box, "fw.txt 가 없다", uiStyleTextBody());
    return;
  }

  for (uint32_t i = 0; i < cnt; i++)
  {
    const prog_proj_t *p = progTaskGetProj(i);
    lv_obj_t          *row;
    lv_obj_t          *l;

    if (p == NULL) break;

    row = uiCreateCard(list_box, LV_PCT(100), PAD * 2 + ROW * 2);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cbItem, LV_EVENT_RELEASED, (void *)(uintptr_t)i);

    // 지금 걸려 있는 것을 표시한다. 반복 작업에서는 이게 먼저 보여야 한다.
    if (strcmp(p->proj, sel_proj) == 0)
    {
      lv_obj_set_style_border_color(row, lv_color_hex(UI_COLOR_ACCENT), 0);
      lv_obj_set_style_border_width(row, 2, 0);
    }

    l = uiCreateLabel(row, p->name, uiStyleTextBody());
    lv_obj_set_style_text_font(l, uiFontCaption(), 0);
    lv_obj_set_width(l, LV_PCT(92));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, LINE(0));

    l = uiCreateLabel(row, p->proj, uiStyleTextDim());
    lv_obj_set_style_text_font(l, uiFontCaption(), 0);
    lv_obj_set_width(l, LV_PCT(92));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, UI_SPACE_MD, LINE(1));
  }
}


// ----------------------------------------------------------------- RUN

static void buildRun(lv_obj_t *parent)
{
  lv_obj_t *btn;

  lbl_big = uiCreateLabel(parent, "0 %", uiStyleTextTitle());
  lv_obj_align(lbl_big, LV_ALIGN_TOP_LEFT, UI_MARGIN, UI_SPACE_SM);

  lbl_phase = uiCreateLabel(parent, "", uiStyleTextDim());
  lv_obj_set_style_text_font(lbl_phase, uiFontCaption(), 0);
  lv_obj_align(lbl_phase, LV_ALIGN_TOP_RIGHT, -UI_MARGIN, UI_SPACE_MD + 10);

  bar_prog = lv_bar_create(parent);
  lv_obj_set_size(bar_prog, body_w, 10);
  lv_obj_align(bar_prog, LV_ALIGN_TOP_MID, 0, 62);
  lv_obj_set_style_bg_color(bar_prog, lv_color_hex(UI_COLOR_SURFACE_ALT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_prog, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar_prog, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(bar_prog, 5, LV_PART_INDICATOR);

  /* 단계 목록. 지나간 단계를 지우지 않고 쌓는다 — 실패했을 때 어디까지 갔었는지가
     가장 알고 싶다. 진행 중에는 자동으로 따라가되 사람이 올리면 멈춘다. */
  step_box = lv_obj_create(parent);
  lv_obj_remove_style_all(step_box);
  lv_obj_set_size(step_box, body_w, 480 - 84 - BTN_H - UI_MARGIN - GAP - 28);
  lv_obj_align(step_box, LV_ALIGN_TOP_MID, 0, 84);
  lv_obj_set_flex_flow(step_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(step_box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(step_box, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(step_box, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(step_box, LV_OPA_50, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(step_box, 4, LV_PART_SCROLLBAR);
  lv_obj_add_event_cb(step_box, cbRunScroll, LV_EVENT_SCROLL_BEGIN, NULL);

  lbl_result = uiCreateLabel(parent, "", uiStyleTextBody());
  lv_obj_set_style_text_font(lbl_result, uiFontCaption(), 0);
  lv_obj_align(lbl_result, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -(BTN_H + UI_MARGIN + GAP));

  btn = uiCreateButton(parent, "ABORT", true);
  lv_obj_set_size(btn, body_w, BTN_H);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -UI_MARGIN);
  lv_obj_add_event_cb(btn, cbStart, LV_EVENT_CLICKED, NULL);
  lbl_abort = lv_obj_get_child(btn, 0);
}

static void runRefresh(bool rebuild)
{
  uint32_t     cnt = progTaskGetStepCnt();
  prog_state_t st  = progTaskGetState();

  if (step_box == NULL) return;

  /* 단계 수가 줄었으면 새 잡이 시작된 것이다. 덧붙이기만 하면 이전 잡의 줄이
     그대로 남고 앞에서부터 덮어써져 뒤쪽에 지난 기록이 남는다.
     "다시 굽기" 로 같은 화면에 머무를 때가 정확히 그 경우다. */
  if (rebuild || cnt < step_drawn)
  {
    lv_obj_clean(step_box);
    step_drawn = 0;
    step_follow = true;
  }

  // 새로 생긴 단계만 붙인다. 매번 다시 그리면 스크롤 위치가 튄다.
  for (uint32_t i = step_drawn; i < cnt; i++)
  {
    lv_obj_t *l = uiCreateLabel(step_box, "", uiStyleTextDim());

    lv_obj_set_style_text_font(l, uiFontCaption(), 0);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  }
  step_drawn = cnt;

  // 내용과 색은 매번 갱신한다 (진행 중인 줄의 퍼센트가 바뀐다)
  for (uint32_t i = 0; i < cnt; i++)
  {
    const prog_step_t *p = progTaskGetStep(i);
    lv_obj_t          *l = lv_obj_get_child(step_box, (int32_t)i);
    const char        *mark;
    uint32_t           col;

    if (p == NULL || l == NULL) break;

    switch (p->state)
    {
      case PROG_STEP_OK:   mark = LV_SYMBOL_OK;    col = UI_COLOR_OK;     break;
      case PROG_STEP_FAIL: mark = LV_SYMBOL_CLOSE; col = UI_COLOR_ACCENT; break;
      default:             mark = LV_SYMBOL_RIGHT; col = UI_COLOR_TEXT;   break;
    }

    if (p->state == PROG_STEP_RUN && p->pct > 0)
    {
      lv_label_set_text_fmt(l, "%s %s%s  %d %%", mark, p->depth ? "   " : "",
                            p->text, (int)p->pct);
    }
    else if (p->ms > 0)
    {
      lv_label_set_text_fmt(l, "%s %s%s  %d.%d s", mark, p->depth ? "   " : "",
                            p->text, (int)(p->ms / 1000), (int)((p->ms % 1000) / 100));
    }
    else
    {
      lv_label_set_text_fmt(l, "%s %s%s", mark, p->depth ? "   " : "", p->text);
    }
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  }

  if (step_follow && cnt > 0)
  {
    lv_obj_scroll_to_view(lv_obj_get_child(step_box, -1), LV_ANIM_OFF);
  }

  // 끝나면 결과를 알린다. 버튼은 그때 뜻이 달라진다.
  if (st == PROG_DONE || st == PROG_ERROR)
  {
    bool ok = (st == PROG_DONE);

    lv_label_set_text_fmt(lbl_result, ok ? "완료  %d.%d 초" : "실패  %d.%d 초",
                          (int)(progTaskGetElapsed() / 1000),
                          (int)((progTaskGetElapsed() % 1000) / 100));
    lv_obj_set_style_text_color(lbl_result,
                                lv_color_hex(ok ? UI_COLOR_OK : UI_COLOR_ACCENT), 0);
    /* 실패했든 중단했든 다음에 할 일은 다시 굽는 것이다. 되돌아가기는 화면
       왼쪽 엣지 스와이프로 언제든 되므로 버튼을 거기 쓰지 않는다. */
    lv_label_set_text(lbl_abort, "다시 굽기");
  }
  else
  {
    lv_label_set_text(lbl_result, "");
    lv_label_set_text(lbl_abort, "ABORT");
  }
}


// ----------------------------------------------------------------- CFG

static void buildCfg(lv_obj_t *parent)
{
  static const char *OPT_NAME[4] = { "굽고 나서", "검증", "병렬도", "SWD 속도" };
  int32_t y = HEAD_H;

  makeHeader(parent, "SETTINGS");

  for (int i = 0; i < 4; i++)
  {
    lv_obj_t *card = makeCard(parent, PAD * 2 + ROW * 2, OPT_NAME[i], NULL);

    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, cbOpt, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

    lbl_opt[i] = makeRowLabel(card, 1, uiStyleTextBody());
    y += PAD * 2 + ROW * 2 + GAP;
  }
}

static void cfgRefresh(void)
{
  static const char *RESET_TXT[3] = { "리셋 후 실행", "리셋 후 정지", "그대로 둔다" };
  static const char *PSIZE_TXT[4] = { "x8  (가장 안전)", "x16", "x32 (VDD 2.7V 이상)", "x64" };
  const prog_opt_t  *o = progTaskGetOpt();

  if (lbl_opt[0] == NULL) return;

  lv_label_set_text(lbl_opt[0], RESET_TXT[o->reset]);
  lv_label_set_text(lbl_opt[1], o->verify ? "한다" : "안 한다");
  lv_label_set_text(lbl_opt[2], PSIZE_TXT[o->psize]);

  if (o->speed_khz == 0) lv_label_set_text(lbl_opt[3], "fw.txt 를 따른다");
  else                   lv_label_set_text_fmt(lbl_opt[3], "%d kHz", (int)o->speed_khz);
}


// ----------------------------------------------------------------- 갱신

static void swdprogUpdate(void)
{
  prog_state_t st;
  uint8_t      pct;
  uint8_t      seq;

  if (page[0] == NULL) return;

  // 슬라이드(200ms)가 끝난 뒤에 워커를 깨운다
  if (kicked == false && (millis() - enter_ms) > 300)
  {
    kicked = true;
    progTaskList();
    progTaskScan();
  }

  st  = progTaskGetState();
  pct = progTaskGetPercent();
  seq = progTaskGetStepSeq();

  // 굽기가 시작되면 실행 화면으로 넘어간다
  if (st == PROG_RUNNING && cur_page != PAGE_RUN)
  {
    step_follow = true;
    runRefresh(true);
    pageShow(PAGE_RUN);
  }

  if (cur_page == PAGE_RUN)
  {
    if (pct != last_pct)
    {
      lv_bar_set_value(bar_prog, pct, LV_ANIM_OFF);
      lv_label_set_text_fmt(lbl_big, "%d %%", (int)pct);
      last_pct = pct;
    }
    if (seq != last_step_seq || st != last_state)
    {
      lv_label_set_text(lbl_phase, progTaskGetPhase());
      runRefresh(false);
      last_step_seq = seq;
    }
  }
  else if (cur_page == PAGE_LIST)
  {
    // 워커가 목록을 다 만들면 그때 그린다 (SD 훑기는 수십 ms 씩 걸린다)
    if (progTaskGetProjSeq() != last_proj_seq)
    {
      last_proj_seq = progTaskGetProjSeq();
      listRefresh();
    }
  }
  else if (cur_page == PAGE_HOME)
  {
    if (st != last_state || seq != last_step_seq)
    {
      homeRefresh();
      last_step_seq = seq;
    }
  }

  last_state = st;
}


// ----------------------------------------------------------------- 이벤트

static void cbScan(lv_event_t *e)
{
  (void)e;

  if (progTaskGetState() == PROG_RUNNING) return;

  progTaskScan();
  last_state = PROG_STATE_CNT;
}

static void cbPick(lv_event_t *e)
{
  (void)e;

  if (progTaskGetState() == PROG_RUNNING) return;

  progTaskList();          // 들어갈 때마다 다시 훑는다 (SD 를 갈아 끼웠을 수 있다)
  listRefresh();           // 있는 것부터 먼저 보여주고, 준비되면 update 가 다시 그린다
  pageShow(PAGE_LIST);
}

static void cbCfgOpen(lv_event_t *e)
{
  (void)e;

  cfgRefresh();
  pageShow(PAGE_CFG);
}

static void cbBack(lv_event_t *e)
{
  (void)e;

  homeRefresh();
  pageShow(PAGE_HOME);
}

/* START / ABORT / 다시 굽기 / 돌아가기 — 상태에 따라 뜻이 달라진다.
   버튼을 여러 개 두는 대신 하나가 그때그때 맞는 일을 한다. */
static void cbStart(lv_event_t *e)
{
  (void)e;

  if (progTaskGetState() == PROG_RUNNING)
  {
    progTaskAbort();
    return;
  }

  /* 완료·실패·중단 어느 쪽이든 다시 굽는다. 실패했으면 배선을 고치고 바로
     재시도하고, 성공했으면 보드만 바꿔 끼면 된다. */
  if (cur_page == PAGE_RUN)
  {
    if (sel_proj[0] != 0)
    {
      step_follow = true;
      progTaskRun(sel_proj);
    }
    return;
  }

  if (sel_proj[0] == 0) return;

  step_follow = true;
  progTaskRun(sel_proj);
}

static void cbRunScroll(lv_event_t *e)
{
  (void)e;

  step_follow = false;      // 사람이 스크롤하면 자동 따라가기를 멈춘다
}

/* 스크롤 플릭이 선택으로 새지 않게 막는다. 여기서 잘못 고르면 엉뚱한 펌웨어를
   굽는다.

   좌표만 보는 가드로는 부족했다. "누른 행 밖에서 뗐나" 만 보면, 스크롤하다
   같은 행 안에서 손을 떼는 흔한 경우가 그대로 통과한다. 실제로 스크롤이
   일어났는지를 물어봐야 한다 - lv_indev_get_scroll_obj 가 그때 non-NULL 이다. */
static void cbItem(lv_event_t *e)
{
  lv_obj_t          *row   = lv_event_get_target(e);
  uint32_t           idx   = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  lv_indev_t        *indev = lv_indev_active();
  const prog_proj_t *p;
  lv_point_t         pt;
  lv_point_t         vect;
  lv_area_t          area;

  if (indev == NULL) return;

  if (lv_indev_get_scroll_obj(indev) != NULL) return;   // 스크롤 중이었다

  // 관성 없이 조금씩 끈 경우도 걸러낸다
  lv_indev_get_vect(indev, &vect);
  if (vect.y > 4 || vect.y < -4) return;

  lv_indev_get_point(indev, &pt);
  lv_obj_get_coords(row, &area);
  if (pt.x < area.x1 || pt.x > area.x2 || pt.y < area.y1 || pt.y > area.y2) return;

  p = progTaskGetProj(idx);
  if (p == NULL) return;

  snprintf(sel_proj, sizeof(sel_proj), "%s", p->proj);
  snprintf(sel_name, sizeof(sel_name), "%s", p->name);
  progTaskSetProject(sel_proj);      // 전원을 껐다 켜도 남는다

  homeRefresh();
  pageShow(PAGE_HOME);
}

// 설정은 탭할 때마다 다음 값으로 돈다. 항목이 적어 이게 가장 빠르다.
static void cbOpt(lv_event_t *e)
{
  static const uint32_t SPD[] = { 0, 400, 1000, 2000, 3500 };
  uint32_t   idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  prog_opt_t o   = *progTaskGetOpt();

  switch (idx)
  {
    case 0: o.reset  = (prog_reset_t)((o.reset + 1) % 3); break;
    case 1: o.verify = !o.verify;                         break;
    case 2: o.psize  = (uint8_t)((o.psize + 1) % 4);      break;
    case 3:
    {
      uint32_t i;

      for (i = 0; i < sizeof(SPD) / sizeof(SPD[0]); i++)
      {
        if (SPD[i] == o.speed_khz) break;
      }
      o.speed_khz = SPD[(i + 1) % (sizeof(SPD) / sizeof(SPD[0]))];
      break;
    }
    default: break;
  }

  progTaskSetOpt(&o);
  cfgRefresh();
}


#endif
