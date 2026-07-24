#include "launcher.h"
#include "ui_shade.h"


#ifdef _USE_HW_LVGL

#define APP_BTN_HEIGHT    88


typedef struct
{
  int32_t     count;
  app_info_t *p_app;

  app_info_t *p_cur;          /* 실행중인 app. NULL 이면 홈 화면 */
  bool        is_exit_req;

  lv_obj_t   *scr_home;
  lv_obj_t   *scr_app;
} launcher_info_t;


static void launcherCreateHome(void);
static void launcherAppBtnCb(lv_event_t *e);
static void launcherEnterApp(app_info_t *p_app);
static void launcherLeaveApp(void);
static int  launcherSortApps(app_info_t **p_list);
#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif

static launcher_info_t info;

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
 * 화면은 런처가 만들어 넘기고, 나갈 때도 런처가 지운다.
 */
void launcherEnterApp(app_info_t *p_app)
{
  lv_obj_t *scr;


  if (p_app->enter == NULL)
    return;

  scr = uiCreateScreen(lv_obj_create(NULL));

  if (p_app->enter(scr) == false)
  {
    lv_obj_delete(scr);
    logPrintf("[NG] %s enter()\n", p_app->name);
    return;
  }

  info.scr_app = scr;
  info.p_cur   = p_app;

  lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

void launcherLeaveApp(void)
{
  lv_obj_t *scr_old = info.scr_app;


  if (info.p_cur == NULL)
    return;

  if (info.p_cur->exit != NULL)
  {
    info.p_cur->exit();
  }

  info.p_cur   = NULL;
  info.scr_app = NULL;

  lv_screen_load_anim(info.scr_home, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);

  /* 전환 애니메이션이 끝난 뒤에 지운다. */
  if (scr_old != NULL)
  {
    lv_obj_delete_delayed(scr_old, 300);
  }
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


  scr = uiCreateScreen(lv_obj_create(NULL));

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

  info.scr_home = scr;
  lv_screen_load(scr);
}

void launcherAppBtnCb(lv_event_t *e)
{
  app_info_t *p_app = (app_info_t *)lv_event_get_user_data(e);

  if (p_app != NULL && info.p_cur == NULL)
  {
    launcherEnterApp(p_app);
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
