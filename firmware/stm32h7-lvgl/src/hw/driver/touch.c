#include "touch.h"


#ifdef _USE_HW_TOUCH
#include "touch/gt911.h"
#include "touch/ft5206.h"
#include "touch/ft5316.h"
#include "cli.h"
#include "cli_gui.h"


static void cliCmd(cli_args_t *args);


static bool is_init = false;
static bool is_enable = false;
static uint16_t touch_width  = HW_LCD_WIDTH;
static uint16_t touch_height = HW_LCD_HEIGHT;

#if defined(_USE_HW_GT911)
#define ic_info_t     gt911_info_t
#define ic_init       gt911Init
#define ic_get_info   gt911GetInfo
#elif defined(_USE_HW_FT5206)
#define ic_info_t     ft5206_info_t
#define ic_init       ft5206Init
#define ic_get_info   ft5206GetInfo
#elif defined(_USE_HW_FT5316)
#define ic_info_t     ft5316_info_t
#define ic_init       ft5316Init
#define ic_get_info   ft5316GetInfo
#endif


bool touchInit(void)
{
  bool ret = false;


  ret = ic_init();
  if (ret == true)
  {
    is_init = true;
    is_enable = true;
  }


  logPrintf("[%s] touchInit()\n", ret ? "OK":"NG");

  cliAdd("touch", cliCmd);

  return ret;
}

bool touchClear(void)
{
  touch_info_t info;

  touchGetInfo(&info);
  
  return true;
}

bool touchGetInfo(touch_info_t *p_info)
{
  bool ret;
  ic_info_t ts_info;

  if (is_init == false) return false;
  if (is_enable == false)
  {
    p_info->count = 0;
    return false;
  } 

  ret = ic_get_info(&ts_info);
  if (ret == true)
  {
    p_info->count = ts_info.count;
    for (int i=0; i<ts_info.count; i++)
    {
      p_info->point[i].event = ts_info.point[i].event;
      p_info->point[i].id    = ts_info.point[i].id;
      p_info->point[i].x     = ts_info.point[i].x;
      p_info->point[i].y     = ts_info.point[i].y;
      p_info->point[i].w     = ts_info.point[i].area;
    }
  }

  return ret;
}

bool touchSetEnable(bool enable)
{
  is_enable = enable;
  return true;
}

bool touchGetEnable(void)
{
  return is_enable;
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init      : %s\n", is_init ? "True" : "False");
    cliPrintf("touch max ch : %d\n", TOUCH_MAX_CH);
    cliPrintf("touch width  : %d\n", touch_width);
    cliPrintf("touch height : %d\n", touch_height);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    touch_info_t info;
    uint32_t pre_time;
    uint32_t exe_time;

    while(cliKeepLoop())
    {
      pre_time = millis();
      if (touchGetInfo(&info) == true)
      {
        exe_time = millis()-pre_time;

        cliPrintf("cnt : %d %3dms, ", info.count, exe_time);

        for (int i=0; i<info.count; i++)
        {
          cliPrintf(" - ");
          cliPrintf("id=%d evt=%2d x=%3d y=%3d w=%3d ", 
            info.point[i].id,      
            info.point[i].event,      
            info.point[i].x, 
            info.point[i].y, 
            info.point[i].w 
            );
        }

        cliPrintf("\n");
      }
      else
      {
        cliPrintf("touchGetInfo() Fail\n");
        break;
      }
      delay(10);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "gui"))
  {
    touch_info_t info;
    touch_info_t info_pre;

    info.count = 0;
    info_pre.count = 0;
    
    cliGui()->initScreen(80, 24);

    while(cliKeepLoop())
    {
      cliGui()->drawBox(0, 0, touch_width/10 + 1, touch_height/20 + 1, "");

      if (touchGetInfo(&info) == true)
      {
        uint16_t x;
        uint16_t y;

        for (int i=0; i<info_pre.count; i++)
        {
          if (info.point[i].x != info_pre.point[i].x || 
              info.point[i].y != info_pre.point[i].y ||
              info.count != info_pre.count)
          {
            x = info_pre.point[i].x/10;
            y = info_pre.point[i].y/20;          
            cliGui()->eraseBox(x, y, 6, 3);
            cliGui()->movePrintf(x+2, y+1, " ");
            cliGui()->movePrintf(x, y+3, "       ");
          }
        }
        for (int i=0; i<info.count; i++)
        {
          x = info.point[i].x/10;
          y = info.point[i].y/20;
          cliGui()->drawBox(x, y, 6, 3, "");
          cliGui()->movePrintf(x+2, y+1, "%d", info.point[i].id);
          cliGui()->movePrintf(x, y+3, "%3d:%3d", info.point[i].x, info.point[i].y);
        }
        info_pre = info;
      }
      delay(10);
    }

    cliGui()->closeScreen();    
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("touch info\n");
    cliPrintf("touch get info\n");
    cliPrintf("touch gui\n");
  }
}

#endif
