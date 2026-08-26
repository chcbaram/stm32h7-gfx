#include "touch/gt911.h"


#ifdef _USE_HW_GT911
#include "i2c.h"
#include "gpio.h"
#include "cli.h"
#include "cli_gui.h"


#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()      
#define unLock()    
#endif

#define GT911_TOUCH_WIDTH         HW_LCD_WIDTH
#define GT911_TOUCH_HEIGHT        HW_LCD_HEIGHT

// 멀티 터치 제한 수이다.
#define GT911_MAX_TOUCH_POINTS    5U

// lcd 내 gt911 내장 플래쉬메모리내 기록할 설정의 바이트수이다.
#define GT911_CONFIG_LENGTH       184U

typedef enum
{
  GT911_CONFIG_4IN,
  GT911_CONFIG_5IN
} gt911_config_model_t;

// 잘못된 패널 설정으로 덮어쓴 GT911을 CLI에서 복구할 때만 사용하는 4인치 설정이다.
static const uint8_t gt911_config_4in[GT911_CONFIG_LENGTH] =
{
  0x61, 0xE0, 0x01, 0xE0, 0x01, 0x05, 0x06, 0x00, 0x01, 0xC8, 0x28, 0x0F,
  0x50, 0x3C, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
  0x1A, 0x1E, 0x14, 0xC5, 0x25, 0x0A, 0xEA, 0xEC, 0xB5, 0x06, 0x00, 0x00,
  0x00, 0x20, 0x21, 0x11, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x2A, 0x64, 0x32,
  0x19, 0x50, 0x32, 0xDC, 0xFA, 0x94, 0x55, 0x02, 0x08, 0x00, 0x00, 0x04,
  0x80, 0xDE, 0x00, 0x80, 0xE4, 0x00, 0x80, 0xEA, 0x00, 0x7F, 0xF0, 0x00,
  0x7F, 0xF6, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x1C, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0E,
  0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x20,
  0x1F, 0x1E, 0x1D, 0x00, 0x02, 0x04, 0x06, 0x08, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// 정상 ER-TPC050-9에서 확인한 공장 설정이며 CLI 복구 명령에서만 사용한다.
static const uint8_t gt911_config_5in[GT911_CONFIG_LENGTH] =
{
  0x5B, 0xE0, 0x01, 0x56, 0x03, 0x05, 0x35, 0x00, 0x01, 0x0A, 0x28, 0x0F,
  0x5A, 0x3C, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17,
  0x18, 0x1E, 0x13, 0x87, 0x29, 0x0A, 0x32, 0x34, 0x0C, 0x08, 0x00, 0x00,
  0x00, 0x03, 0x02, 0x1D, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x28, 0x55, 0x94, 0xD5, 0x02, 0x07, 0x00, 0x00, 0x04,
  0x8D, 0x2B, 0x00, 0x80, 0x32, 0x00, 0x75, 0x3A, 0x00, 0x6C, 0x43, 0x00,
  0x64, 0x4F, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x4A, 0x3A, 0xFF,
  0xFF, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x14, 0x12, 0x10, 0x0E, 0x0C, 0x0A, 0x08, 0x06,
  0x04, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
  0x04, 0x06, 0x08, 0x0A, 0x0C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x24,
  0x26, 0x28, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

static void cliCmd(cli_args_t *args);
static bool readRegs(uint16_t reg_addr, void *p_data, uint32_t length);
static bool writeRegs(uint16_t reg_addr, void *p_data, uint32_t length);

static uint8_t i2c_ch   = _DEF_I2C1;
static uint8_t i2c_addr = 0x5D; 
static bool is_init = false;
static bool is_detected = false;
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock = NULL;
#endif




bool gt911Init(void)
{
  bool ret = false;

  #ifdef _USE_HW_RTOS
  if (mutex_lock == NULL)
  {
    mutex_lock = xSemaphoreCreateMutex();
  }
  #endif

  // I2C Addr for 0x5D
  //
  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_LOW);
  gpioPinWrite(_PIN_GPIO_LCD_TS_INT, _DEF_LOW);
  delay(10);
  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_HIGH);
  delay(50);
  gpioPinMode(_PIN_GPIO_LCD_TS_INT, _DEF_INPUT);


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

  is_init = ret;
  
  logPrintf("[%s] gt911Init()\n", ret ? "OK":"NG");

  cliAdd("gt911", cliCmd);

  i2c_addr = 0x5D; 


  return ret;
}

static bool readRegs(uint16_t reg_addr, void *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, reg_addr, p_data, length, 10);
  unLock();
  return ret;
}

static bool writeRegs(uint16_t reg_addr, void *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cWriteA16Bytes(i2c_ch, i2c_addr, reg_addr, p_data, length, 10);
  unLock();
  return ret;
}

static bool gt911RestoreConfig(gt911_config_model_t model)
{
  const uint8_t *p_source;
  uint8_t config[GT911_CONFIG_LENGTH];
  uint8_t update[2] = {0};
  uint8_t readback[GT911_CONFIG_LENGTH];
  uint8_t readback_update[2];
  uint8_t sum = 0;


  p_source = model == GT911_CONFIG_4IN ? gt911_config_4in : gt911_config_5in;
  memcpy(config, p_source, sizeof(config));

  // 버전을 0으로 기록해 현재 저장 버전과 관계없이 복구 설정을 적용한다.
  config[0] = 0;
  for (uint32_t i=0; i<GT911_CONFIG_LENGTH; i++)
  {
    update[0] += config[i];
  }
  update[0] = (uint8_t)((~update[0]) + 1U);
  update[1] = 1U;

  if (writeRegs(GT911_CONFIG_START, config, GT911_CONFIG_LENGTH) == false ||
      writeRegs(GT911_CONFIG_CHKSUM, update, sizeof(update)) == false)
  {
    return false;
  }

  delay(100);

  if (readRegs(GT911_CONFIG_START, readback, sizeof(readback)) == false ||
      readRegs(GT911_CONFIG_CHKSUM, readback_update, sizeof(readback_update)) == false)
  {
    return false;
  }

  for (uint32_t i=0; i<sizeof(readback); i++)
  {
    sum += readback[i];
  }
  sum += readback_update[0];

  // GT911이 적용 과정에서 버전 바이트를 바꿀 수 있어 나머지 설정과 체크섬을 검증한다.
  if (sum != 0U || readback_update[1] != 0U ||
      memcmp(&readback[1], &config[1], GT911_CONFIG_LENGTH - 1U) != 0)
  {
    return false;
  }

  return true;
}

static void gt911PrintConfigInfo(void)
{
  uint8_t config[GT911_CONFIG_LENGTH];
  uint8_t checksum;
  uint8_t sum = 0;
  uint16_t width;
  uint16_t height;


  if (readRegs(GT911_CONFIG_START, config, sizeof(config)) == false ||
      readRegs(GT911_CONFIG_CHKSUM, &checksum, 1) == false)
  {
    cliPrintf("GT911 config read failed\n");
    return;
  }

  for (uint32_t i=0; i<sizeof(config); i++)
  {
    sum += config[i];
  }
  sum += checksum;

  width  = ((uint16_t)config[2] << 8) | config[1];
  height = ((uint16_t)config[4] << 8) | config[3];

  cliPrintf("size     : %d x %d\n", width, height);
  cliPrintf("checksum : %s (0x%02X)\n", sum == 0U ? "valid" : "invalid", checksum);
}

uint16_t gt911GetWidth(void)
{
  return GT911_TOUCH_WIDTH;
}

uint16_t gt911GetHeight(void)
{
  return GT911_TOUCH_HEIGHT;
}

bool gt911GetInfo(gt911_info_t *p_info)
{
  if (p_info == NULL)
  {
    return false;
  }
  p_info->count = 0U;

  if (is_init == false)
  {
    return false;
  }

#if LCD_MODEL_5_0_480x854
  bool ret;
  uint8_t status = 0;
  uint8_t clear_status = 0;
  uint8_t touch_cnt;
  uint8_t point_data[GT911_MAX_TOUCH_POINTS * 8U] = {0};

  ret = readRegs(GT911_POINT_INFO, &status, 1);
  if (ret == false)
  {
    return false;
  }
  if ((status & 0x80U) == 0U)
  {
    return true;
  }

  touch_cnt = status & 0x0FU;
  if (touch_cnt > GT911_MAX_TOUCH_POINTS)
  {
    (void)writeRegs(GT911_POINT_INFO, &clear_status, 1);
    return false;
  }

  if (touch_cnt > 0U)
  {
    ret = readRegs(GT911_POINT_1, point_data, (uint32_t)touch_cnt * 8U);
    if (ret == false)
    {
      return false;
    }
  }

  if (writeRegs(GT911_POINT_INFO, &clear_status, 1) == false)
  {
    return false;
  }

  for (uint32_t i=0; i<touch_cnt; i++)
  {
    const uint8_t *buf = &point_data[i * 8U];

    p_info->point[i].id     = buf[0];
    p_info->point[i].event  = 0;
    p_info->point[i].weight = 0;
    p_info->point[i].area   = ((uint16_t)buf[6] << 8) | buf[5];
    p_info->point[i].x      = ((uint16_t)buf[2] << 8) | buf[1];
    p_info->point[i].y      = ((uint16_t)buf[4] << 8) | buf[3];
  }
  p_info->count = touch_cnt;

  return true;
#else
  bool ret = false;
  uint8_t buf[8] = {0};
  uint8_t touch_cnt = 0;
  const uint16_t point_addr[] = GT911_POINTS_REG;

  if (gpioPinRead(_PIN_GPIO_LCD_TS_INT) == _DEF_HIGH)
  {
    return true;
  }

  ret = readRegs(GT911_POINT_INFO, buf, 1);
  if (ret == true && (buf[0] & 0x80U) != 0U)
  {
    touch_cnt = buf[0] & 0x0FU;
    if (touch_cnt > GT911_MAX_TOUCH_POINTS)
    {
      buf[0] = 0;
      (void)writeRegs(GT911_POINT_INFO, buf, 1);
      return false;
    }

    buf[0] = 0;
    ret = writeRegs(GT911_POINT_INFO, buf, 1);
  }
  else
  {
    return ret;
  }

  if (ret == false)
  {
    return false;
  }

  p_info->count = touch_cnt;
  for (uint32_t i=0; i<touch_cnt; i++)
  {
    ret = readRegs(point_addr[i], buf, sizeof(buf));
    if (ret == false)
    {
      p_info->count = i;
      break;
    }

    p_info->point[i].id     = buf[0];
    p_info->point[i].event  = 0;
    p_info->point[i].weight = 0;
    p_info->point[i].area   = ((uint16_t)buf[6] << 8) | buf[5];
    p_info->point[i].x      = ((uint16_t)buf[2] << 8) | buf[1];
    p_info->point[i].y      = ((uint16_t)buf[4] << 8) | buf[3];
  }

  return ret;
#endif
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init     : %s\n", is_init ? "True" : "False");
    cliPrintf("is_detected : %s\n", is_detected ? "True" : "False");
    cliPrintf("is_addr     : 0x%02X\n", i2c_addr);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "config") && args->isStr(1, "info"))
  {
    gt911PrintConfigInfo();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "restore") && args->isStr(1, "4in"))
  {
    cliPrintf("GT911 4-inch config restore %s\n",
              gt911RestoreConfig(GT911_CONFIG_4IN) ? "success" : "failed");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "restore") && args->isStr(1, "5in"))
  {
    cliPrintf("GT911 5-inch config restore %s\n",
              gt911RestoreConfig(GT911_CONFIG_5IN) ? "success" : "failed");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint16_t addr;
    uint16_t len;
    uint8_t data;

    addr = args->getData(1);
    len  = args->getData(2);

    for (int i=0; i<len; i++)
    {
      if (readRegs(addr + i, &data, 1) == true)
      {
        cliPrintf("0x%04x : 0x%02X\n", addr + i, data);
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
    uint16_t addr;
    uint8_t  data;

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
    gt911_info_t info;
    uint32_t pre_time;
    uint32_t exe_time;

    cliPrintf(" \n");
    cliPrintf("ready\n");

    while(cliKeepLoop())
    {
      pre_time = millis();
      if (gt911GetInfo(&info) == true)
      {
        exe_time = millis()-pre_time;

        if (info.count > 0)
        {
          cliPrintf("cnt : %d %3dms, ", info.count, exe_time);
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
      }
      else
      {
        cliPrintf("gt911GetInfo() Fail\n");
        break;
      }
      delay(10);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "gui"))
  {
    gt911_info_t info;
    gt911_info_t info_pre;

    info.count = 0;
    info_pre.count = 0;
    cliGui()->initScreen(80, 24);

    while(cliKeepLoop())
    {
      cliGui()->drawBox(0, 0, 480/10 + 1, 480/20 + 1, "");

      if (gt911GetInfo(&info) == true)
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
    cliPrintf("gt911 info\n");
    cliPrintf("gt911 config info\n");
    cliPrintf("gt911 restore 4in\n");
    cliPrintf("gt911 restore 5in\n");
    cliPrintf("gt911 read addr len[0~255]\n");
    cliPrintf("gt911 write addr data \n");
    cliPrintf("gt911 get info\n");
    cliPrintf("gt911 gui\n");
  }
}

#endif
