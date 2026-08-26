#include "touch/gsl1680.h"


#ifdef _USE_HW_GSL1680
#include "i2c.h"
#include "gpio.h"
#include "touch/gsl1680_fw.h"


#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif

#define GSL1680_TOUCH_WIDTH      HW_LCD_WIDTH
#define GSL1680_TOUCH_HEIGHT     HW_LCD_HEIGHT
#define GSL1680_I2C_ADDR         0x40U
#define GSL1680_REG_TOUCH_DATA   0x80U
#define GSL1680_REG_STATUS       0xE0U
#define GSL1680_REG_CTL          0xE4U
#define GSL1680_REG_PAGE         0xF0U
#define GSL1680_TOUCH_DATA_LEN   ((GSL1680_MAX_CONTACTS + 1U) * 4U)

// 기존 5인치 패널은 컨트롤러의 X/Y 축이 LCD 축과 교차되어 있다.
#define GSL1680_SWAP_XY          1
#define GSL1680_INVERT_X         0
#define GSL1680_INVERT_Y         0
#define GSL1680_MAP_X_RAW_MAX    800U
#define GSL1680_MAP_Y_RAW_MAX    480U


static bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length);
static bool writeRegs(uint8_t reg_addr, const uint8_t *p_data, uint32_t length);
static bool writeRegU32(uint8_t reg_addr, uint32_t data);
static bool gsl1680InitRegs(void);
static bool gsl1680LoadFw(void);
static bool gsl1680CheckMem(void);
static bool gsl1680ClrReg(void);
static bool gsl1680ResetChip(void);
static bool gsl1680StartupChip(void);
static uint16_t scaleAxis(uint16_t raw, uint16_t raw_max, uint16_t out_max);
static void transformPoint(uint16_t raw_x, uint16_t raw_y, uint16_t *p_x, uint16_t *p_y);


static uint8_t i2c_ch = _DEF_I2C1;
static bool is_init = false;
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock = NULL;
#endif


bool gsl1680Init(void)
{
  bool ret;

#ifdef _USE_HW_RTOS
  if (mutex_lock == NULL)
  {
    mutex_lock = xSemaphoreCreateMutex();
  }
#endif

  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_LOW);
  delay(20);
  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_HIGH);
  delay(20);
  gpioPinMode(_PIN_GPIO_LCD_TS_INT, _DEF_INPUT);

  if (i2cIsBegin(i2c_ch) == false)
  {
    ret = i2cBegin(i2c_ch, 400);
  }
  else
  {
    ret = true;
  }

  if (ret == true)
  {
    ret = i2cIsDeviceReady(i2c_ch, GSL1680_I2C_ADDR);
  }
  if (ret == true)
  {
    // GSL1680은 내부 RAM에서 동작하므로 리셋할 때마다 펌웨어를 전송한다.
    ret = gsl1680InitRegs();
  }

  is_init = ret;
  logPrintf("[%s] gsl1680Init()\n", ret ? "OK":"NG");
  return ret;
}

static bool gsl1680InitRegs(void)
{
  bool ret;

  ret = gsl1680ClrReg();
  if (ret == true) ret = gsl1680ResetChip();
  if (ret == true) ret = gsl1680LoadFw();
  if (ret == true) ret = gsl1680StartupChip();
  if (ret == true) ret = gsl1680ResetChip();
  if (ret == true) ret = gsl1680StartupChip();
  if (ret == true) ret = gsl1680CheckMem();
  return ret;
}

static bool gsl1680ClrReg(void)
{
  bool ret = true;
  uint8_t data;

  data = 0x88;
  ret &= writeRegs(GSL1680_REG_STATUS, &data, 1);
  delay(20);

  data = 0x01;
  ret &= writeRegs(GSL1680_REG_TOUCH_DATA, &data, 1);
  delay(5);

  data = 0x04;
  ret &= writeRegs(GSL1680_REG_CTL, &data, 1);
  delay(5);

  data = 0x00;
  ret &= writeRegs(GSL1680_REG_STATUS, &data, 1);
  delay(20);
  return ret;
}

static bool gsl1680ResetChip(void)
{
  bool ret = true;
  const uint8_t clear_data[4] = {0, };
  uint8_t data;

  data = 0x88;
  ret &= writeRegs(GSL1680_REG_STATUS, &data, 1);
  delay(10);

  data = 0x04;
  ret &= writeRegs(GSL1680_REG_CTL, &data, 1);
  delay(10);

  ret &= writeRegs(0xBC, clear_data, sizeof(clear_data));
  delay(10);
  return ret;
}

static bool gsl1680StartupChip(void)
{
  bool ret = true;

  ret &= writeRegU32(GSL1680_REG_PAGE, 0x01FE1000);
  ret &= writeRegU32(0x04, 0x0000000F);
  delay(20);
  ret &= writeRegU32(GSL1680_REG_STATUS, 0x00000000);
  delay(10);
  return ret;
}

static bool gsl1680LoadFw(void)
{
  bool ret = true;
  uint8_t data[4];

  for (uint32_t i=0; i<sizeof(GSL1680_FW)/sizeof(GSL1680_FW[0]); i++)
  {
    if (GSL1680_FW[i].offset == GSL1680_REG_PAGE)
    {
      data[0] = (uint8_t)(GSL1680_FW[i].val & 0xFFU);
      ret = writeRegs(GSL1680_REG_PAGE, data, 1);
    }
    else
    {
      data[0] = (uint8_t)((GSL1680_FW[i].val >> 0)  & 0xFFU);
      data[1] = (uint8_t)((GSL1680_FW[i].val >> 8)  & 0xFFU);
      data[2] = (uint8_t)((GSL1680_FW[i].val >> 16) & 0xFFU);
      data[3] = (uint8_t)((GSL1680_FW[i].val >> 24) & 0xFFU);
      ret = writeRegs(GSL1680_FW[i].offset, data, sizeof(data));
    }

    if (ret == false)
    {
      break;
    }
  }
  return ret;
}

static bool gsl1680CheckMem(void)
{
  uint8_t data[4] = {0, };
  bool ret;

  delay(30);
  ret = readRegs(0xB0, data, sizeof(data));
  if (ret == true && data[0] == 0x5A && data[1] == 0x5A &&
      data[2] == 0x5A && data[3] == 0x5A)
  {
    return true;
  }

  // 펌웨어 확인이 실패한 경우 한 번만 하드웨어 리셋 후 다시 올린다.
  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_LOW);
  delay(20);
  gpioPinWrite(_PIN_GPIO_LCD_TS_RST, _DEF_HIGH);
  delay(20);

  ret = gsl1680ClrReg();
  if (ret == true) ret = gsl1680ResetChip();
  if (ret == true) ret = gsl1680LoadFw();
  if (ret == true) ret = gsl1680StartupChip();
  if (ret == true) ret = gsl1680ResetChip();
  if (ret == true) ret = gsl1680StartupChip();
  return ret;
}

static bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cWriteData(i2c_ch, GSL1680_I2C_ADDR, &reg_addr, 1, 10);
  if (ret == true)
  {
    ret = i2cReadData(i2c_ch, GSL1680_I2C_ADDR, p_data, length, 50);
  }
  unLock();
  return ret;
}

static bool writeRegs(uint8_t reg_addr, const uint8_t *p_data, uint32_t length)
{
  bool ret;
  uint8_t wr_buf[5];

  if (length > (sizeof(wr_buf) - 1U))
  {
    return false;
  }

  wr_buf[0] = reg_addr;
  for (uint32_t i=0; i<length; i++)
  {
    wr_buf[i + 1U] = p_data[i];
  }

  lock();
  ret = i2cWriteData(i2c_ch, GSL1680_I2C_ADDR, wr_buf, length + 1U, 10);
  unLock();
  return ret;
}

static bool writeRegU32(uint8_t reg_addr, uint32_t data)
{
  uint8_t buf[4];

  buf[0] = (uint8_t)((data >> 0)  & 0xFFU);
  buf[1] = (uint8_t)((data >> 8)  & 0xFFU);
  buf[2] = (uint8_t)((data >> 16) & 0xFFU);
  buf[3] = (uint8_t)((data >> 24) & 0xFFU);
  return writeRegs(reg_addr, buf, sizeof(buf));
}

static uint16_t scaleAxis(uint16_t raw, uint16_t raw_max, uint16_t out_max)
{
  uint32_t scaled;

  if (raw_max == 0U || out_max == 0U)
  {
    return 0;
  }

  scaled = ((uint32_t)raw * out_max) / raw_max;
  if (scaled >= out_max)
  {
    scaled = out_max - 1U;
  }
  return (uint16_t)scaled;
}

static void transformPoint(uint16_t raw_x, uint16_t raw_y, uint16_t *p_x, uint16_t *p_y)
{
  uint16_t map_x;
  uint16_t map_y;

#if GSL1680_SWAP_XY
  map_x = raw_y;
  map_y = raw_x;
#else
  map_x = raw_x;
  map_y = raw_y;
#endif

  *p_x = scaleAxis(map_x, GSL1680_MAP_X_RAW_MAX, GSL1680_TOUCH_WIDTH);
  *p_y = scaleAxis(map_y, GSL1680_MAP_Y_RAW_MAX, GSL1680_TOUCH_HEIGHT);

#if GSL1680_INVERT_X
  *p_x = (GSL1680_TOUCH_WIDTH - 1U) - *p_x;
#endif
#if GSL1680_INVERT_Y
  *p_y = (GSL1680_TOUCH_HEIGHT - 1U) - *p_y;
#endif
}

uint16_t gsl1680GetWidth(void)
{
  return GSL1680_TOUCH_WIDTH;
}

uint16_t gsl1680GetHeight(void)
{
  return GSL1680_TOUCH_HEIGHT;
}

bool gsl1680GetInfo(gsl1680_info_t *p_info)
{
  uint8_t buf[GSL1680_TOUCH_DATA_LEN] = {0, };
  uint8_t touch_cnt;

  if (p_info == NULL || is_init == false)
  {
    return false;
  }
  p_info->count = 0;

  if (readRegs(GSL1680_REG_TOUCH_DATA, buf, sizeof(buf)) == false)
  {
    return false;
  }

  touch_cnt = buf[0];
  if (touch_cnt > GSL1680_MAX_CONTACTS)
  {
    touch_cnt = GSL1680_MAX_CONTACTS;
  }

  for (uint8_t i=0; i<touch_cnt; i++)
  {
    uint8_t offset = 4U * (i + 1U);
    uint16_t raw_x;
    uint16_t raw_y;

    p_info->point[i].id     = buf[offset + 3U] >> 4;
    p_info->point[i].event  = 0;
    p_info->point[i].weight = 10;
    p_info->point[i].area   = 10;

    raw_x = ((uint16_t)(buf[offset + 3U] & 0x0FU) << 8) | buf[offset + 2U];
    raw_y = ((uint16_t)buf[offset + 1U] << 8) | buf[offset];
    transformPoint(raw_x, raw_y, &p_info->point[i].x, &p_info->point[i].y);
  }

  p_info->count = touch_cnt;
  return true;
}

#endif
