#include "launcher.h"


#ifdef _USE_HW_LVGL

#define APP_BTN_HEIGHT    72


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

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), LV_PART_MAIN);

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

  /* 홈으로 되돌린 뒤 app 화면을 지운다.
   * 전환 애니메이션이 끝나기 전에 지우면 안 되므로 auto_del 로 맡긴다.
   */
  lv_screen_load_anim(info.scr_home, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);

  if (scr_old != NULL)
  {
    lv_obj_delete_delayed(scr_old, 300);
  }
}

void launcherCreateHome(void)
{
  lv_obj_t *scr;
  lv_obj_t *label;
  lv_obj_t *list;
  uint16_t  order_min;
  uint16_t  order_pre;


  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), LV_PART_MAIN);
  lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);

  label = lv_label_create(scr);
  lv_label_set_text(label, _DEF_BOARD_NAME);
  lv_obj_set_style_text_color(label, lv_color_hex(0x8090A0), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

  list = lv_obj_create(scr);
  lv_obj_set_size(list, LCD_WIDTH - 60, LCD_HEIGHT - 90);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);

  /* .app 섹션은 링크 순서라 order 값으로 정렬해 배치한다. */
  order_pre = 0;
  for (int n = 0; n < info.count; n++)
  {
    app_info_t *p_sel = NULL;

    order_min = 0xFFFF;
    for (int i = 0; i < info.count; i++)
    {
      if (info.p_app[i].enter == NULL)
        continue;
      if (n > 0 && info.p_app[i].order < order_pre)
        continue;
      if (info.p_app[i].order < order_min)
      {
        order_min = info.p_app[i].order;
        p_sel     = &info.p_app[i];
      }
    }
    if (p_sel == NULL)
      break;

    order_pre = order_min + 1;

    lv_obj_t *btn = lv_button_create(list);
    lv_obj_set_size(btn, LV_PCT(100), APP_BTN_HEIGHT);
    lv_obj_add_event_cb(btn, launcherAppBtnCb, LV_EVENT_CLICKED, p_sel);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, (const char *)p_sel->name);
    lv_obj_center(btn_label);
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

  if (args->argc == 2 && args->isStr(0, "run"))
  {
    ret = launcherRunApp(args->getStr(1));
    cliPrintf("%s\n", ret ? "OK":"not found or busy");
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
