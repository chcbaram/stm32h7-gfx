#include "cassette.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

#define REEL_SPOKE_CNT      3
#define REEL_HUB_R          16
#define REEL_PACK_R_MIN     22
#define REEL_PACK_R_MAX     52

#define TAPE_TOTAL_MS       (3*60*1000)   /* 껍데기 단계의 가상 길이 */


typedef enum
{
  CASSETTE_STOP,
  CASSETTE_PLAY,
  CASSETTE_REC,
  CASSETTE_FF,
  CASSETTE_REW,
} CassetteState_t;

typedef struct
{
  lv_obj_t *pack;                       /* 감긴 테이프 뭉치 */
  lv_obj_t *hub;
  lv_obj_t *spoke[REEL_SPOKE_CNT];
  int32_t   cx;
  int32_t   cy;
} reel_t;


static bool cassetteEnter(lv_obj_t *scr);
static void cassetteUpdate(void);
static void cassetteExit(void);

static void createShell(lv_obj_t *parent);
static void createReel(lv_obj_t *parent, reel_t *p_reel, int32_t cx, int32_t cy);
static void updateReel(reel_t *p_reel, int32_t angle, int32_t pack_r);
static void setState(CassetteState_t state);
static void btnEventCb(lv_event_t *e);


static reel_t   reel_l;
static reel_t   reel_r;
static lv_obj_t *label_title;
static lv_obj_t *label_time;
static lv_obj_t *label_state;

static CassetteState_t cassette_state = CASSETTE_STOP;
static int32_t  reel_angle = 0;
static int32_t  pos_ms     = 0;
static uint32_t pre_time   = 0;




bool cassetteInit(void)
{
  return true;
}

bool cassetteEnter(lv_obj_t *scr)
{
  createShell(scr);

  cassette_state = CASSETTE_STOP;
  reel_angle = 0;
  pos_ms     = 0;
  pre_time   = millis();

  setState(CASSETTE_STOP);
  return true;
}

void cassetteExit(void)
{
  /* TODO 오디오 연결 후 : 재생/녹음 정지, 파일 닫기 */
  cassette_state = CASSETTE_STOP;
}

void cassetteUpdate(void)
{
  uint32_t now = millis();
  int32_t  diff_ms;
  int32_t  speed = 0;
  int32_t  pack_l;
  int32_t  pack_r;


  diff_ms  = (int32_t)(now - pre_time);
  pre_time = now;

  switch(cassette_state)
  {
    case CASSETTE_PLAY:
    case CASSETTE_REC:
      speed = 1;
      break;
    case CASSETTE_FF:
      speed = 8;
      break;
    case CASSETTE_REW:
      speed = -8;
      break;
    default:
      speed = 0;
      break;
  }

  if (speed != 0)
  {
    pos_ms += diff_ms * speed;
    if (pos_ms < 0)            pos_ms = 0;
    if (pos_ms > TAPE_TOTAL_MS) pos_ms = TAPE_TOTAL_MS;

    /* 릴 반지름이 커질수록 각속도는 느려진다. */
    reel_angle = (reel_angle + (diff_ms * speed) / 4) % 360;
    if (reel_angle < 0) reel_angle += 360;
  }

  /* 왼쪽은 풀리고 오른쪽은 감긴다. */
  pack_l = REEL_PACK_R_MAX - ((REEL_PACK_R_MAX - REEL_PACK_R_MIN) * pos_ms) / TAPE_TOTAL_MS;
  pack_r = REEL_PACK_R_MIN + ((REEL_PACK_R_MAX - REEL_PACK_R_MIN) * pos_ms) / TAPE_TOTAL_MS;

  updateReel(&reel_l, reel_angle, pack_l);
  updateReel(&reel_r, reel_angle, pack_r);

  lv_label_set_text_fmt(label_time, "%02d:%02d", (int)(pos_ms/1000)/60, (int)(pos_ms/1000)%60);
}

void createShell(lv_obj_t *parent)
{
  lv_obj_t *shell;
  lv_obj_t *label_win;
  lv_obj_t *btn_area;
  const char *btn_txt[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_STOP, LV_SYMBOL_NEXT};
  const int   btn_id[]  = {CASSETTE_REW, CASSETTE_PLAY, CASSETTE_STOP, CASSETTE_FF};


  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);

  /* --- 카세트 셸 --- */
  shell = lv_obj_create(parent);
  lv_obj_set_size(shell, 420, 260);
  lv_obj_align(shell, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_radius(shell, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(shell, lv_color_hex(0x2A2E33), LV_PART_MAIN);
  lv_obj_set_style_border_color(shell, lv_color_hex(0x4A5058), LV_PART_MAIN);
  lv_obj_set_style_border_width(shell, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(shell, 0, LV_PART_MAIN);
  lv_obj_remove_flag(shell, LV_OBJ_FLAG_SCROLLABLE);

  /* --- 라벨 --- */
  label_win = lv_obj_create(shell);
  lv_obj_set_size(label_win, 380, 76);
  lv_obj_align(label_win, LV_ALIGN_TOP_MID, 0, 14);
  lv_obj_set_style_radius(label_win, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_color(label_win, lv_color_hex(0xE8E4D8), LV_PART_MAIN);
  lv_obj_set_style_border_width(label_win, 0, LV_PART_MAIN);
  lv_obj_remove_flag(label_win, LV_OBJ_FLAG_SCROLLABLE);

  label_title = lv_label_create(label_win);
  lv_label_set_text(label_title, "No Tape");
  lv_obj_set_style_text_color(label_title, lv_color_hex(0x202020), LV_PART_MAIN);
  lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 4, -10);

  label_state = lv_label_create(label_win);
  lv_label_set_text(label_state, "STOP");
  lv_obj_set_style_text_color(label_state, lv_color_hex(0x707070), LV_PART_MAIN);
  lv_obj_align(label_state, LV_ALIGN_LEFT_MID, 4, 14);

  label_time = lv_label_create(label_win);
  lv_label_set_text(label_time, "00:00");
  lv_obj_set_style_text_color(label_time, lv_color_hex(0x707070), LV_PART_MAIN);
  lv_obj_align(label_time, LV_ALIGN_RIGHT_MID, -4, 14);

  /* --- 테이프 창 + 릴 --- */
  createReel(shell, &reel_l, -95, 40);
  createReel(shell, &reel_r,  95, 40);

  /* --- 조작부 --- */
  btn_area = lv_obj_create(parent);
  lv_obj_set_size(btn_area, 440, 100);
  lv_obj_align(btn_area, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_opa(btn_area, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn_area, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(btn_area, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(btn_area, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_area, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(btn_area, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 4; i++)
  {
    lv_obj_t *btn = lv_button_create(btn_area);
    lv_obj_set_size(btn, 84, 68);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A4048), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, btnEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)btn_id[i]);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, btn_txt[i]);
    lv_obj_center(label);
  }

  /* --- 녹음 버튼 --- */
  lv_obj_t *btn_rec = lv_button_create(parent);
  lv_obj_set_size(btn_rec, 84, 44);
  lv_obj_align(btn_rec, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_radius(btn_rec, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn_rec, lv_color_hex(0xC0392B), LV_PART_MAIN);
  lv_obj_add_event_cb(btn_rec, btnEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)CASSETTE_REC);

  lv_obj_t *label_rec = lv_label_create(btn_rec);
  lv_label_set_text(label_rec, "REC");
  lv_obj_center(label_rec);

  /* --- 뒤로 --- */
  lv_obj_t *btn_back = lv_button_create(parent);
  lv_obj_set_size(btn_back, 60, 44);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -8);
  lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x3A4048), LV_PART_MAIN);
  lv_obj_add_event_cb(btn_back, btnEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back, LV_SYMBOL_LEFT);
  lv_obj_center(label_back);
}

void createReel(lv_obj_t *parent, reel_t *p_reel, int32_t cx, int32_t cy)
{
  p_reel->cx = cx;
  p_reel->cy = cy;

  /* 감긴 테이프 */
  p_reel->pack = lv_obj_create(parent);
  lv_obj_set_size(p_reel->pack, REEL_PACK_R_MIN*2, REEL_PACK_R_MIN*2);
  lv_obj_align(p_reel->pack, LV_ALIGN_CENTER, cx, cy);
  lv_obj_set_style_radius(p_reel->pack, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(p_reel->pack, lv_color_hex(0x14171A), LV_PART_MAIN);
  lv_obj_set_style_border_width(p_reel->pack, 0, LV_PART_MAIN);
  lv_obj_remove_flag(p_reel->pack, LV_OBJ_FLAG_SCROLLABLE);

  /* 허브 */
  p_reel->hub = lv_obj_create(parent);
  lv_obj_set_size(p_reel->hub, REEL_HUB_R*2, REEL_HUB_R*2);
  lv_obj_align(p_reel->hub, LV_ALIGN_CENTER, cx, cy);
  lv_obj_set_style_radius(p_reel->hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(p_reel->hub, lv_color_hex(0xC8CCD0), LV_PART_MAIN);
  lv_obj_set_style_border_width(p_reel->hub, 0, LV_PART_MAIN);
  lv_obj_remove_flag(p_reel->hub, LV_OBJ_FLAG_SCROLLABLE);

  /* 회전이 보이도록 허브에 스포크를 얹는다. */
  for (int i = 0; i < REEL_SPOKE_CNT; i++)
  {
    p_reel->spoke[i] = lv_obj_create(parent);
    lv_obj_set_size(p_reel->spoke[i], 8, 8);
    lv_obj_set_style_radius(p_reel->spoke[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p_reel->spoke[i], lv_color_hex(0x40464C), LV_PART_MAIN);
    lv_obj_set_style_border_width(p_reel->spoke[i], 0, LV_PART_MAIN);
    lv_obj_remove_flag(p_reel->spoke[i], LV_OBJ_FLAG_SCROLLABLE);
  }
}

void updateReel(reel_t *p_reel, int32_t angle, int32_t pack_r)
{
  lv_obj_set_size(p_reel->pack, pack_r*2, pack_r*2);
  lv_obj_align(p_reel->pack, LV_ALIGN_CENTER, p_reel->cx, p_reel->cy);

  for (int i = 0; i < REEL_SPOKE_CNT; i++)
  {
    int32_t a = angle + (360 / REEL_SPOKE_CNT) * i;
    int32_t x = (lv_trigo_cos(a) * (REEL_HUB_R - 6)) / LV_TRIGO_SIN_MAX;
    int32_t y = (lv_trigo_sin(a) * (REEL_HUB_R - 6)) / LV_TRIGO_SIN_MAX;

    lv_obj_align(p_reel->spoke[i], LV_ALIGN_CENTER, p_reel->cx + x, p_reel->cy + y);
  }
}

void setState(CassetteState_t state)
{
  const char *str[] = {"STOP", "PLAY", "REC", "FF", "REW"};

  cassette_state = state;
  lv_label_set_text(label_state, str[state]);

  /* TODO 오디오 연결 후 :
   *   PLAY -> i2s 로 SD 의 wav 재생
   *   REC  -> pdmRecordStart() 후 SD 에 wav 로 저장
   */
}

void btnEventCb(lv_event_t *e)
{
  int id = (int)(intptr_t)lv_event_get_user_data(e);

  if (id < 0)
  {
    launcherExitApp();
    return;
  }

  setState((CassetteState_t)id);
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
