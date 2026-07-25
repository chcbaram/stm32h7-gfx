#include "launcher.h"
#include "ui_shade.h"


#ifdef _USE_HW_LVGL


#define APP_BTN_HEIGHT    88

/* 왼쪽 가장자리에서 오른쪽으로 쓸면 뒤로가기.
 * 센서는 위젯이 없는 왼쪽 여백(UI_MARGIN) 안에만 두어 탭과 충돌하지 않게 한다.
 */
#define BACK_EDGE_W       24


#define SLIDE_MS          200


typedef struct
{
  int32_t     count;
  app_info_t *p_app;

  app_info_t *p_cur;          /* 실행중인 app. NULL 이면 홈 화면 */
  bool        is_exit_req;

  lv_obj_t   *scr_root;       /* 항상 로드되어 있는 루트 스크린 */
  lv_obj_t   *home_cont;      /* 홈 화면 컨테이너 (루트의 자식) */
  lv_obj_t   *app_cont;       /* 실행중 app 컨테이너 (홈 위에 올라감) */
} launcher_info_t;


static void launcherCreateHome(void);
static void launcherAppBtnCb(lv_event_t *e);
static void launcherEnterApp(app_info_t *p_app);
static void launcherLeaveApp(void);
static int  launcherSortApps(app_info_t **p_list);
static void launcherBackSwipeCb(lv_event_t *e);
static void launcherSlideAppTo(int32_t x_from, int32_t x_to, bool close_after);
static void animXCb(void *obj, int32_t v);
static void animCloseDoneCb(lv_anim_t *a);
#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif

static launcher_info_t info;

static int32_t back_x0;
static int32_t back_y0;
static bool    back_armed;
static bool    back_fired;

extern uint32_t _sapp;
extern uint32_t _eapp;




bool launcherInit(void)
{
  bool ret = true;


  info.count = ((int)&_eapp - (int)&_sapp) / sizeof(app_info_t);
  info.p_app = (app_info_t *)&_sapp;
  info.p_cur = NULL;
  info.is_exit_req = false;

  logPrintf("[  ] launcherInit()\n");
  logPrintf("       app count : %d\n", (int)info.count);

  for (int i = 0; i < info.count; i++)
  {
    bool app_ret = true;

    if (info.p_app[i].init != NULL)
    {
      app_ret = info.p_app[i].init();
      ret &= app_ret;
    }
    logPrintf("       %s %s\n", info.p_app[i].name, app_ret ? "OK":"Fail");
  }

  launcherCreateHome();

  /* 왼쪽 가장자리 백스와이프 센서 (모든 app 공통) */
  {
    lv_obj_t *sensor = lv_obj_create(lv_layer_top());

    lv_obj_remove_style_all(sensor);
    lv_obj_set_size(sensor, BACK_EDGE_W, LCD_HEIGHT);
    lv_obj_align(sensor, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(sensor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sensor, launcherBackSwipeCb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(sensor, launcherBackSwipeCb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(sensor, launcherBackSwipeCb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(sensor, launcherBackSwipeCb, LV_EVENT_PRESS_LOST, NULL);
  }

#ifdef _USE_HW_CLI
  cliAdd("launcher", cliCmd);
#endif
  return ret;
}

/* ui 스레드에서 주기적으로 불린다. */
void launcherUpdate(void)
{
  if (info.is_exit_req == true)
  {
    info.is_exit_req = false;
    launcherLeaveApp();
    return;
  }

  if (info.p_cur != NULL && info.p_cur->update != NULL)
  {
    info.p_cur->update();
  }
}

bool launcherRunApp(const char *name)
{
  if (info.p_cur != NULL)
    return false;

  for (int i = 0; i < info.count; i++)
  {
    if (strncmp((const char *)info.p_app[i].name, name, sizeof(info.p_app[i].name)) == 0)
    {
      launcherEnterApp(&info.p_app[i]);
      return true;
    }
  }
  return false;
}

void launcherExitApp(void)
{
  if (info.p_cur != NULL)
  {
    info.is_exit_req = true;
  }
}

const char *launcherGetAppName(void)
{
  if (info.p_cur == NULL)
    return NULL;

  return (const char *)info.p_cur->name;
}

/* app 진입.
 * 홈 컨테이너 위에 app 컨테이너를 만들어 오른쪽에서 밀어 넣는다.
 */
void launcherEnterApp(app_info_t *p_app)
{
  lv_obj_t *cont;


  if (p_app->enter == NULL || info.p_cur != NULL)
    return;

  cont = uiCreateScreen(lv_obj_create(info.scr_root));
  lv_obj_set_size(cont, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(cont, LCD_WIDTH, 0);          /* 화면 오른쪽 밖 */

  if (p_app->enter(cont) == false)
  {
    lv_obj_delete(cont);
    logPrintf("[NG] %s enter()\n", p_app->name);
    return;
  }

  info.app_cont = cont;
  info.p_cur    = p_app;

  /* 홈은 그대로 두고 app 만 오른쪽 밖에서 왼쪽으로 밀어 들어온다. */
  launcherSlideAppTo(LCD_WIDTH, 0, false);
}

/* app 종료. 현재 위치에서 오른쪽 밖으로 밀어내고 지운다. */
void launcherLeaveApp(void)
{
  if (info.p_cur == NULL)
    return;

  if (info.p_cur->exit != NULL)
  {
    info.p_cur->exit();
  }

  info.p_cur = NULL;
  launcherSlideAppTo(lv_obj_get_x(info.app_cont), LCD_WIDTH, true);
}

void animXCb(void *obj, int32_t v)
{
  lv_obj_set_x((lv_obj_t *)obj, v);
}

void animCloseDoneCb(lv_anim_t *a)
{
  lv_obj_delete((lv_obj_t *)a->var);
  if ((lv_obj_t *)a->var == info.app_cont)
    info.app_cont = NULL;
}

/* app 컨테이너를 x_from 에서 x_to 로 애니메이션. close_after 면 끝나고 삭제.
 * (막 만든 컨테이너는 lv_obj_get_x 가 아직 갱신 전이라 시작값을 명시한다.)
 */
void launcherSlideAppTo(int32_t x_from, int32_t x_to, bool close_after)
{
  lv_anim_t a;

  if (info.app_cont == NULL)
    return;

  lv_obj_set_x(info.app_cont, x_from);

  lv_anim_init(&a);
  lv_anim_set_var(&a, info.app_cont);
  lv_anim_set_exec_cb(&a, animXCb);
  lv_anim_set_values(&a, x_from, x_to);
  lv_anim_set_duration(&a, SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  if (close_after == true)
    lv_anim_set_completed_cb(&a, animCloseDoneCb);
  lv_anim_start(&a);
}

/* .app 섹션은 링크 순서라 order 값으로 정렬한다. */
int launcherSortApps(app_info_t **p_list)
{
  int cnt = 0;

  for (int i = 0; i < info.count; i++)
  {
    if (info.p_app[i].enter == NULL)
      continue;
    p_list[cnt++] = &info.p_app[i];
  }

  for (int i = 1; i < cnt; i++)
  {
    app_info_t *key = p_list[i];
    int j = i - 1;

    while (j >= 0 && p_list[j]->order > key->order)
    {
      p_list[j + 1] = p_list[j];
      j--;
    }
    p_list[j + 1] = key;
  }
  return cnt;
}

void launcherCreateHome(void)
{
  lv_obj_t   *scr;
  lv_obj_t   *label;
  lv_obj_t   *bar;
  lv_obj_t   *list;
  app_info_t *sorted[32];
  int         cnt;


  /* 루트 스크린은 항상 로드되어 있고, 그 위에 홈/ app 컨테이너가 올라간다.
   * app 을 오른쪽으로 밀면 뒤에 홈이 드러나는 인터랙티브 전환을 위해서다.
   */
  info.scr_root = uiCreateScreen(lv_obj_create(NULL));
  lv_screen_load(info.scr_root);

  scr = uiCreateScreen(lv_obj_create(info.scr_root));
  lv_obj_set_size(scr, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(scr, 0, 0);

  /* --- 상단 바 ---
   * 제목을 화면 한가운데 띄워놓는 대신 바닥을 깔아 층을 만든다.
   */
  bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, LCD_WIDTH, 104);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  label = uiCreateLabel(bar, UI_TITLE, uiStyleTextTitle());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -10);

  label = uiCreateLabel(bar, _DEF_FIRMWATRE_VERSION, uiStyleTextDim());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 24);

  /* 강조색 밑줄 하나로 화면에 색을 넣는다. */
  bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 72, 4);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 104);
  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);

  /* --- app 목록 --- */
  list = lv_obj_create(scr);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, LCD_WIDTH - UI_MARGIN*2, LCD_HEIGHT - 104 - UI_SPACE_LG*2);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 104 + UI_SPACE_LG);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, UI_SPACE_MD, LV_PART_MAIN);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  cnt = launcherSortApps(sorted);
  if (cnt > (int)(sizeof(sorted)/sizeof(sorted[0])))
    cnt = sizeof(sorted)/sizeof(sorted[0]);

  for (int i = 0; i < cnt; i++)
  {
    lv_obj_t *btn = uiCreateButton(list, NULL, false);
    lv_obj_t *dot;

    lv_obj_set_size(btn, LV_PCT(100), APP_BTN_HEIGHT);
    lv_obj_set_style_radius(btn, UI_RADIUS_MD, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, launcherAppBtnCb, LV_EVENT_CLICKED, sorted[i]);

    /* 왼쪽에 색점 하나. 아이콘이 생기면 이 자리를 대체한다. */
    dot = lv_obj_create(btn);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, UI_SPACE_LG, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);

    label = uiCreateLabel(btn, (const char *)sorted[i]->name, uiStyleTextBody());
    lv_obj_align(label, LV_ALIGN_LEFT_MID, UI_SPACE_LG + 26, 0);

    label = uiCreateLabel(btn, LV_SYMBOL_RIGHT, uiStyleTextDim());
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, -UI_SPACE_LG, 0);
  }

  info.home_cont = scr;
}

void launcherAppBtnCb(lv_event_t *e)
{
  app_info_t *p_app = (app_info_t *)lv_event_get_user_data(e);

  if (p_app != NULL && info.p_cur == NULL)
  {
    launcherEnterApp(p_app);
  }
}

/* 왼쪽에서 오른쪽으로 끌면 현재 app 화면이 손가락을 따라 밀리고,
 * 절반 넘게 밀고 놓으면 뒤로가기(홈), 아니면 제자리로 되돌아간다.
 */
void launcherBackSwipeCb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;


  lv_indev_get_point(lv_indev_active(), &p);

  if (code == LV_EVENT_PRESSED)
  {
    back_x0    = p.x;
    back_y0    = p.y;
    back_fired = false;                        /* 아직 드래그로 확정 안 됨 */
    /* app 이 열려 있고, 전환 애니메이션 중이 아니고, 셰이드가 닫혀 있을 때만 */
    back_armed = (info.p_cur != NULL) &&
                 (info.app_cont != NULL) &&
                 (lv_obj_get_x(info.app_cont) == 0) &&
                 (ui_shade_is_open() == false);
  }
  else if (code == LV_EVENT_PRESSING && back_armed == true)
  {
    int32_t dx = p.x - back_x0;
    int32_t dy = p.y - back_y0;
    int32_t ady = dy < 0 ? -dy : dy;

    /* 가로 이동이 세로보다 크면 드래그로 확정하고 화면을 따라 민다. */
    if (back_fired == false && dx > 10 && dx > ady)
      back_fired = true;

    if (back_fired == true)
    {
      if (dx < 0) dx = 0;
      lv_obj_set_x(info.app_cont, dx);
    }
  }
  else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
           back_armed == true && back_fired == true && info.app_cont != NULL)
  {
    back_armed = false;

    /* 절반 넘게 밀렸으면 뒤로가기, 아니면 제자리로 */
    if (lv_obj_get_x(info.app_cont) > LCD_WIDTH / 2)
      launcherExitApp();                       /* leaveApp 가 마저 밀어냄 */
    else
      launcherSlideAppTo(lv_obj_get_x(info.app_cont), 0, false);
  }
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("count : %d\n", (int)info.count);
    cliPrintf("cur   : %s\n", info.p_cur != NULL ? (const char *)info.p_cur->name : "-");
    for (int i = 0; i < info.count; i++)
    {
      cliPrintf("%d : %-16s order %d\n", i, info.p_app[i].name, info.p_app[i].order);
    }
    ret = true;
  }

  /* app 이름에 공백이 들어갈 수 있어 나머지 인자를 다시 이어붙인다. */
  if (args->argc >= 2 && args->isStr(0, "run"))
  {
    char name[32] = {0, };
    bool run_ret;

    for (int i = 1; i < args->argc; i++)
    {
      uint32_t len = strlen(name);

      if (i > 1 && len < sizeof(name) - 1)
      {
        name[len++] = ' ';
        name[len]   = 0;
      }
      strncat(name, args->getStr(i), sizeof(name) - strlen(name) - 1);
    }

    run_ret = launcherRunApp(name);
    cliPrintf("%s : %s\n", name, run_ret ? "OK":"not found or busy");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "exit"))
  {
    launcherExitApp();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("launcher info\n");
    cliPrintf("launcher run [name]\n");
    cliPrintf("launcher exit\n");
  }
}
#endif

#endif
