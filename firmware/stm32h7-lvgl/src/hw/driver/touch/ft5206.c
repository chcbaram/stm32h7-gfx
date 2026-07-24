#include "touch/ft5206.h"



#ifdef _USE_HW_FT5206
#include "i2c.h"
#include "gpio.h"
#include "cli.h"
#include "cli_gui.h"

#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);


#define FT5206_TOUCH_WIDTH    HW_LCD_WIDTH
#define FT5206_TOUCH_HEIGTH   HW_LCD_HEIGHT

#define FT6246_PIN_RESET      _PIN_GPIO_LCD_TS_RST



static void cliCmd(cli_args_t *args);
static bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length);
static bool writeRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length);
static bool ft5206InitRegs(void);


static uint8_t i2c_ch   = _DEF_I2C1;
static uint8_t i2c_addr = 0x38; 
static bool is_init = false;
static bool is_detected = false;
static SemaphoreHandle_t mutex_lock = NULL;





bool ft5206Init(void)
{
  bool ret = false;


  if (mutex_lock == NULL)
  {
    mutex_lock = xSemaphoreCreateMutex();
  }

  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_LOW);
  delay(50);
  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_HIGH);
  delay(50);


  if (i2cIsBegin(i2c_ch) == true)
    ret = true;
  else
    ret = i2cBegin(i2c_ch, 400);


  if (ret == true && i2cIsDeviceReady(i2c_ch, i2c_addr))
  {    
    is_detected = true;
  }
  else
  {
    ret = false;
  }

  if (is_detected == true)
  {
    ret = ft5206InitRegs();
  }

  is_init = ret;

  logPrintf("[%s] ft5206Init()\n", ret ? "OK":"NG");

  cliAdd("ft5206", cliCmd);

  return ret;
}

bool ft5206InitRegs(void)
{
  uint8_t data;

  data = 0;  
  writeRegs(FT5206_REG_DEV_MODE, &data, 1);
  return true;
}

bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length)
{
  bool ret;

  lock();
  // Set Reg Addr
  ret = i2cWriteData(i2c_ch, i2c_addr, &reg_addr, 1, 10);
  if (ret == true)
  {
    // Read Data
    ret = i2cReadData(i2c_ch, i2c_addr, p_data, length, 50);
  } 
  unLock();

  return ret;
}

bool writeRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length)
{
  bool ret;
  uint8_t wr_buf[length + 1];


  wr_buf[0] = reg_addr;
  for (int i=0; i<length; i++)
  {
    wr_buf[1+i] = p_data[i];
  }
  ret = i2cWriteData(i2c_ch, i2c_addr, wr_buf, length+1, 10);

  return ret;
}

uint16_t ft5206GetWidth(void)
{
  return FT5206_TOUCH_WIDTH;
}

uint16_t ft5206GetHeight(void)
{
  return FT5206_TOUCH_HEIGTH;
}

bool ft5206GetInfo(ft5206_info_t *p_info)
{
  bool ret;
  uint8_t buf[14];

  if (is_init == false)
  {
    p_info->count = 0;
    return false;
  }

  ret = readRegs(0x00, buf, 14);
  if (ret == true)
  {
    p_info->gest_id = buf[FT5206_REG_GEST_ID];
    p_info->count   = buf[FT5206_REG_TD_STATUS] & 0x0F;
    if (p_info->count <= FT5206_MAX_TOUCH_POINT)
    {
      for (int i=0; i<p_info->count; i++)
      {
        uint16_t x;
        uint16_t y;

        p_info->point[i].id     = (buf[FT5206_REG_P_YH     + (6*i)] & 0xF0) >> 4;
        p_info->point[i].event  = (buf[FT5206_REG_P_XH     + (6*i)] & 0xC0) >> 6;
        p_info->point[i].weight = 20;
        p_info->point[i].area   = 20;

        x  = (buf[FT5206_REG_P_XH + (6*i)] & 0x0F) << 8;
        x |= (buf[FT5206_REG_P_XL + (6*i)] & 0xFF) << 0;
        y  = (buf[FT5206_REG_P_YH + (6*i)] & 0x0F) << 8;
        y |= (buf[FT5206_REG_P_YL + (6*i)] & 0xFF) << 0;

        p_info->point[i].x = x;
        p_info->point[i].y = y; 
      }
    }
    else
    {
      ret = false;
    }
  }

  return ret;
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint8_t reg_data;

    cliPrintf("is_init     : %s\n", is_init ? "True" : "False");
    cliPrintf("is_detected : %s\n", is_detected ? "True" : "False");

    readRegs(FT5206_REG_DEV_MODE, &reg_data, 1);
    cliPrintf("DEV_MODE    : 0x%02X (%d)\n", reg_data, reg_data);

    readRegs(FT5206_REG_TH_GROUP, &reg_data, 1);
    cliPrintf("TH_GROUP    : 0x%02X (%d)\n", reg_data, reg_data);

    readRegs(FT5206_REG_TH_DIFF, &reg_data, 1);
    cliPrintf("TH_DIFF     : 0x%02X (%d)\n", reg_data, reg_data);

    readRegs(FT5206_REG_CTRL, &reg_data, 1);
    cliPrintf("CTRL        : 0x%02X (%d)\n", reg_data, reg_data);

    readRegs(FT5206_REG_PERIOID_ACTIVE, &reg_data, 1);
    cliPrintf("PERIOID A   : 0x%02X (%d)\n", reg_data, reg_data);

    readRegs(FT5206_REG_PERIOID_MONITOR, &reg_data, 1);
    cliPrintf("PERIOID M   : 0x%02X (%d)\n", reg_data, reg_data);

    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint8_t addr;
    uint8_t len;
    uint8_t data;

    addr = args->getData(1);
    len  = args->getData(2);

    for (int i=0; i<len; i++)
    {
      if (readRegs(addr + i, &data, 1) == true)
      {
        cliPrintf("0x%02x : 0x%02X\n", addr + i, data);
      }
      else
      {
        cliPrintf("readRegs() Fail\n");
        break;
      }
    }

    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write"))
  {
    uint8_t addr;
    uint8_t data;

    addr = args->getData(1);
    data = args->getData(2);


    if (writeRegs(addr, &data, 1) == true)
    {
      cliPrintf("0x%02x : 0x%02X\n", addr, data);
    }
    else
    {
      cliPrintf("writeRegs() Fail\n");
    }

    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    ft5206_info_t info;
    uint32_t pre_time;
    uint32_t exe_time;

    while(cliKeepLoop())
    {
      pre_time = millis();
      if (ft5206GetInfo(&info) == true)
      {
        exe_time = millis()-pre_time;

        cliPrintf("cnt : %d %3dms, g=%d ", info.count, exe_time, info.gest_id);

        for (int i=0; i<info.count; i++)
        {
          cliPrintf(" - ");
          cliPrintf("id=%d evt=%2d x=%3d y=%3d w=%3d a=%3d ", 
            info.point[i].id,      
            info.point[i].event,      
            info.point[i].x, 
            info.point[i].y, 
            info.point[i].weight, 
            info.point[i].area
            );
        }

        cliPrintf("\n");
      }
      else
      {
        cliPrintf("ft5206GetInfo() Fail\n");
        break;
      }
      delay(10);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "gui"))
  {
    ft5206_info_t info;
    ft5206_info_t info_pre;

    info.count = 0;
    info_pre.count = 0;
    cliGui()->initScreen(80, 24);

    while(cliKeepLoop())
    {
      cliGui()->drawBox(0, 0, 480/10 + 1, 320/20 + 1, "");

      if (ft5206GetInfo(&info) == true)
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
    cliPrintf("ft5206 info\n");
    cliPrintf("ft5206 read addr[0~0xFF] len[0~255]\n");
    cliPrintf("ft5206 write addr[0~0xFF] data \n");
    cliPrintf("ft5206 get info\n");
    cliPrintf("ft5206 gui\n");
  }
}

#endif