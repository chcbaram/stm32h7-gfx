#include "touch/ak4183.h"

#ifdef _USE_HW_AK4183
#include "i2c.h"
#include "gpio.h"
#include "cli.h"
#include "cli_gui.h"
#include "eeprom.h"

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif

#undef AK4183_TCH_POINT_ADC_TRIM

#define AK4183_TOUCH_WIDTH    HW_LCD_WIDTH
#define AK4183_TOUCH_HEIGHT   HW_LCD_HEIGHT

#define AK4183_EEPROM_MAGIC_NUMBER   0x34313833 // "4183"

#define AK4183_DEFAULT_ADC_X1   512
#define AK4183_DEFAULT_ADC_X2   529
#define AK4183_DEFAULT_ADC_X3   3535
#define AK4183_DEFAULT_ADC_X4   3535
#define AK4183_DEFAULT_ADC_X5   2087

#define AK4183_DEFAULT_ADC_Y1   1011
#define AK4183_DEFAULT_ADC_Y2   3376
#define AK4183_DEFAULT_ADC_Y3   3367
#define AK4183_DEFAULT_ADC_Y4   993
#define AK4183_DEFAULT_ADC_Y5   2243

typedef struct
{
  uint32_t x_adc;
  uint32_t y_adc;
} ak4183_adc_t;





static void cliCmd(cli_args_t *args);
static bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length);
static bool ak4183InitRegs(void);
static bool ak4183ReadAdc(ak4183_adc_t *p_adc);
static bool ak4183IsReady(void);

static void do_calibration(int touch_x, int touch_y, int *px, int *py);
static int calculate_calibration_coefficient(uint8_t points, uint32_t* screen_x,  uint32_t* screen_y, uint32_t* cali_x, uint32_t* cali_y);



static uint8_t i2c_ch   = _DEF_I2C1;
static uint8_t i2c_addr = 0x48;
static bool is_cali = false;
static bool is_init = false;
static bool is_detected = false;
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock = NULL;
#endif

/* 좌표값 (수정 금지)*/
static uint32_t x_scr_ref[5] = {90, 90, 700, 700, 400};
static uint32_t y_scr_ref[5] = {90, 400, 400, 90, 250};


static uint32_t x_buf[5] = {512,  529,  3535, 3535, 2087};
static uint32_t y_buf[5] = {1011, 3376, 3367, 993,  2243};

double KX1, KX2, KX3, KY1, KY2, KY3;             // coefficients for calibration algorithm

static ak4183_cali_t tch_adc;



bool ak4183Init(void)
{
  bool ret = false;

  #ifdef _USE_HW_RTOS
  if (mutex_lock == NULL)
  {
    mutex_lock = xSemaphoreCreateMutex();
  }
  #endif

  gpioPinMode(_PIN_GPIO_LCD_TS_INT, _DEF_INPUT);


  if (i2cIsBegin(i2c_ch) == true)
    ret = true;
  else
    ret = i2cBegin(i2c_ch, 400);

  if (eepromIsInit())
  {
    ak4183touchDataRead(&tch_adc);
    if (tch_adc.tch_magic_number != AK4183_EEPROM_MAGIC_NUMBER)
    {
      logPrintf("[E_] ak4183 magic number failed\n");
      tch_adc.x_adc[0] = AK4183_DEFAULT_ADC_X1;
      tch_adc.x_adc[1] = AK4183_DEFAULT_ADC_X2;
      tch_adc.x_adc[2] = AK4183_DEFAULT_ADC_X3;
      tch_adc.x_adc[3] = AK4183_DEFAULT_ADC_X4;
      tch_adc.x_adc[4] = AK4183_DEFAULT_ADC_X5;

      tch_adc.y_adc[0] = AK4183_DEFAULT_ADC_Y1;
      tch_adc.y_adc[1] = AK4183_DEFAULT_ADC_Y2;
      tch_adc.y_adc[2] = AK4183_DEFAULT_ADC_Y3;
      tch_adc.y_adc[3] = AK4183_DEFAULT_ADC_Y4;
      tch_adc.y_adc[4] = AK4183_DEFAULT_ADC_Y5;
      ak4183touchDataWrite(&tch_adc);
    }
  }

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
    ret = ak4183InitRegs();
  }

  is_init = ret;

  logPrintf("[%s] ak4183Init()\n", ret ? "OK":"NG");

  cliAdd("ak4183", cliCmd);

  return ret;
}

bool ak4183InitRegs(void)
{
  uint32_t* p_x_buf = tch_adc.x_adc;
  uint32_t* p_y_buf = tch_adc.y_adc;
  int calibration_coefficient = calculate_calibration_coefficient(5, x_scr_ref, y_scr_ref, p_x_buf, p_y_buf);

  if(calibration_coefficient == 0)
  {
    logPrintf("[  ] no cali\n");
  }
  else
  {
    is_cali = true;
    logPrintf("[  ] cali ok\n");
  }

  return true;
}

bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cReadBytes(i2c_ch, i2c_addr, reg_addr, p_data, length, 50);
  unLock();

  return ret;
}


#define MAX_POINT_ERROR 10





// do calibration for point(Px, Py) using the calculated coefficients 
void do_calibration(int touch_x, int touch_y, int *px, int *py)
{
  *px = (unsigned int)(KX1 * (touch_x) + KX2 * (touch_y) + KX3 + 0.5);
  *py = (unsigned int)(KY1 * (touch_x) + KY2 * (touch_y) + KY3 + 0.5);
}

bool check_error_analysis(uint8_t points, uint32_t* screen_x,  uint32_t* screen_y, uint32_t* cali_x, uint32_t* cali_y)
{
  int i;
  uint32_t maxErr, err;
  int x, y;
  int dx, dy;
  uint32_t errThreshold = MAX_POINT_ERROR; // Can be overridden by registry entry
                                           /*
                                           396, 236
                                           76,    48
                                           80,   428
                                           716, 432
                                           720,   44
                                           */
  maxErr = 0;
  for (i = 0; i < points; i++)
  {
    do_calibration(cali_x[i], cali_y[i], &x, &y);
    // x /= 4;
    // y /= 4;

    // printk("%4d, %4d  : %4d, %4d => %4d, %4d\r\n", cali_x[i], cali_y[i], screen_x[i], screen_y[i], x, y);

    dx = x - screen_x[i];
    dy = y - screen_y[i];
    err = dx * dx + dy * dy;
    if (err > maxErr)
    {
      maxErr = err;
    }
  }
  logPrintf("Calibration error rate: %ld\r\n", maxErr);

  if (maxErr < (errThreshold * errThreshold))
  {
    return true;
  }
  else
  {
    return false;
  }
}

// calculate the coefficients for calibration algorithm: KX1, KX2, KX3, KY1, KY2, KY3
int calculate_calibration_coefficient(uint8_t points, uint32_t* screen_x,  uint32_t* screen_y, uint32_t* cali_x, uint32_t* cali_y)
{
  int i;
  double a[3], b[3], c[3], d[3], k;
  if (points < 3)
  {
    return 0;
  }
  else
  {
    if (points == 3)
    {
      for (i = 0; i < points; i++)
      {
        a[i] = (double)(cali_x[i]);
        b[i] = (double)(cali_y[i]);
        c[i] = (double)(screen_x[i]);
        d[i] = (double)(screen_y[i]);
      }
    }
    else if (points > 3)
    {
      for (i = 0; i < 3; i++)
      {
        a[i] = 0;
        b[i] = 0;
        c[i] = 0;
        d[i] = 0;
      }
      for (i = 0; i < points; i++)
      {
        a[2] = a[2] + (double)(cali_x[i]);
        b[2] = b[2] + (double)(cali_y[i]);
        c[2] = c[2] + (double)(screen_x[i]);
        d[2] = d[2] + (double)(screen_y[i]);

        a[0] = a[0] + (double)(cali_x[i]) * (double)(cali_x[i]);
        a[1] = a[1] + (double)(cali_x[i]) * (double)(cali_y[i]);
        b[0] = a[1];
        b[1] = b[1] + (double)(cali_y[i]) * (double)(cali_y[i]);
        c[0] = c[0] + (double)(cali_x[i]) * (double)(screen_x[i]);
        c[1] = c[1] + (double)(cali_y[i]) * (double)(screen_x[i]);
        d[0] = d[0] + (double)(cali_x[i]) * (double)(screen_y[i]);
        d[1] = d[1] + (double)(cali_y[i]) * (double)(screen_y[i]);
      }
      a[0] = a[0] / a[2];
      a[1] = a[1] / b[2];
      b[0] = b[0] / a[2];
      b[1] = b[1] / b[2];
      c[0] = c[0] / a[2];
      c[1] = c[1] / b[2];
      d[0] = d[0] / a[2];
      d[1] = d[1] / b[2];
      a[2] = a[2] / points;
      b[2] = b[2] / points;
      c[2] = c[2] / points;
      d[2] = d[2] / points;
    }
    k = (a[0] - a[2]) * (b[1] - b[2]) - (a[1] - a[2]) * (b[0] - b[2]);
    KX1 = ((c[0] - c[2]) * (b[1] - b[2]) - (c[1] - c[2]) * (b[0] - b[2])) / k;
    KX2 = ((c[1] - c[2]) * (a[0] - a[2]) - (c[0] - c[2]) * (a[1] - a[2])) / k;
    KX3 = (b[0] * (a[2] * c[1] - a[1] * c[2]) + b[1] * (a[0] * c[2] - a[2] * c[0]) + b[2] * (a[1] * c[0] - a[0] * c[1])) / k;
    KY1 = ((d[0] - d[2]) * (b[1] - b[2]) - (d[1] - d[2]) * (b[0] - b[2])) / k;
    KY2 = ((d[1] - d[2]) * (a[0] - a[2]) - (d[0] - d[2]) * (a[1] - a[2])) / k;
    KY3 = (b[0] * (a[2] * d[1] - a[1] * d[2]) + b[1] * (a[0] * d[2] - a[2] * d[0]) + b[2] * (a[1] * d[0] - a[0] * d[1])) / k;

    // debugprintf("%f %f %f %f %f %f %f\n",KX1, KX2, KX3, KY1, KY2, KY3);

    return check_error_analysis(points, screen_x, screen_y, cali_x, cali_y);
  }
}

bool ak4183ReadAdc(ak4183_adc_t *p_adc)
{
  bool ret;
  uint8_t rx_buf[2];

  // x-axis
  ret = i2cReadBytes(i2c_ch, i2c_addr, 0xC4, rx_buf, 2, 50);
  if (!ret)
    return false;
  p_adc->x_adc = (rx_buf[0]<<4) | (rx_buf[1]>>4);

  // y-axis
  ret = i2cReadBytes(i2c_ch, i2c_addr, 0xD4, rx_buf, 2, 50);
  if (!ret)
    return false;
  p_adc->y_adc = (rx_buf[0]<<4) | (rx_buf[1]>>4);

  return true;
}

bool ak4183IsReady(void)
{
  return gpioPinRead(_PIN_GPIO_LCD_TS_INT) == _DEF_LOW;
}

uint16_t ak4183GetWidth(void)
{
  return AK4183_TOUCH_WIDTH;
}

uint16_t ak4183GetHeight(void)
{
  return AK4183_TOUCH_HEIGHT;
}

bool ak4183GetInfo(ak4183_info_t *p_info)
{
  bool ret;

  if (is_init == false)
  {
    p_info->count = 0;
    return false;
  }

  if (is_cali == false)
  {
    p_info->count = 0;
    return false;
  }


  p_info->count = 0;

  if (gpioPinRead(_PIN_GPIO_LCD_TS_INT) == _DEF_HIGH)
  {
    return true;
  }

  ak4183_adc_t adc_data;

  ret = ak4183ReadAdc(&adc_data);
  if (ret == true)
  {
    p_info->gest_id = 0;
    p_info->count   = 1;
    for (int i=0; i<p_info->count; i++)
    {
      int cal_x;
      int cal_y;

      do_calibration(adc_data.x_adc, adc_data.y_adc,  (int*)&cal_x,  (int*)&cal_y);


      p_info->point[i].id     = 0;
      p_info->point[i].event  = 0;
      p_info->point[i].weight = 20;
      p_info->point[i].area   = 20;

      p_info->point[i].x = cal_x;
      p_info->point[i].y = cal_y;
    }
  }

  return ret;
}

bool ak4183CalibrationProc(int16_t x, int16_t y)
{
  bool ret = false;

  if (x < 0 || y < 0)
  {
    return false;
  }

  ak4183_adc_t coordinate;
  
  coordinate.x_adc = (uint32_t)x;
  coordinate.y_adc = (uint32_t)y;  
  
  if (ak4183ReadAdc(&coordinate))
  {
    logPrintf("[  ] coordinate.x : %d, coordinate.y : %d\n", coordinate.x_adc, coordinate.y_adc);

    // ADC 값 정렬 및 잘라내기 (배열)
    //
    // 평균값
    //
    

    ret = true;
  }

  return ret;
}

#ifdef AK4183_TCH_POINT_ADC_TRIM
#define ADC_TRIM_CNT      (2)
#define CYCLE     20

int ak4183GetAdc(void)
{
	int avg = 0;
	int sum = 0;
	int tmp[CYCLE];

	for(int i=0;i<CYCLE;i++)
	{
	  tmp[i] = hx711_get_raw();
	  HAL_Delay(13);
	}

	for (uint8_t i = 0; i < (CYCLE-1); i++)
	{
		for (uint8_t j = (i+1); j < CYCLE; j++)
		{
			if (tmp[i] > tmp[j]) 
			{
				SWAP(tmp[i], tmp[j]); 
			}
		}
	}

	printf("------------\r\n");
	for (uint8_t i = 0; i < CYCLE; i++)
	{
		printf("%ld \r\n", tmp[i]);		
	}	

	for (uint8_t i = ADC_TRIM_CNT; i < (CYCLE-ADC_TRIM_CNT); i++)
	{
		sum += tmp[i];
	}

	avg = sum/(CYCLE-(ADC_TRIM_CNT*2));

	return avg;
}
#endif 

bool ak4183SaveCaliData(tch_cali_info_t tch_info)
{
  bool ret = false;

  if (eepromIsInit())
  {
    ak4183touchDataWrite(&tch_adc);
    ret = true;
  }
  else
  {
    logPrintf("[E_] ak4183SaveCaliData()\n");
  }

  return ret;
}

bool ak4183IsCaliResultErr(tch_cali_info_t tch_info)
{
  bool ret = false;
  uint8_t next_state = tch_info.state + 1;

  if (next_state != R_TOUCH_CALI_END)
  {
    return false;
  }

  int result = calculate_calibration_coefficient(5, x_scr_ref, y_scr_ref, tch_info.x_adc, tch_info.y_adc);

  if (result == 0)
  {
    ret = false;
  }
  else
  {
    ret = true;
  }

  return ret;
}

bool ak4183touchDataWrite(ak4183_cali_t* p_data)
{
  bool ret = false;
  uint16_t tch_adc_data_size = sizeof(ak4183_cali_t);

  p_data->tch_magic_number = AK4183_EEPROM_MAGIC_NUMBER;
  ret = eepromWrite(HW_EEPROM_ADDR_TOUCH, (uint8_t*)p_data, tch_adc_data_size);

  return ret;
}


bool ak4183touchDataRead(ak4183_cali_t* p_data)
{
  bool ret = false;
  uint16_t tch_adc_data_size = sizeof(ak4183_cali_t);
  ret = eepromRead(HW_EEPROM_ADDR_TOUCH, (uint8_t*)p_data, tch_adc_data_size);

  return ret;
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init     : %s\n", is_init ? "True" : "False");
    cliPrintf("is_detected : %s\n", is_detected ? "True" : "False");
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

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "adc"))
  {
    ak4183_adc_t adc_info;
    uint32_t pre_time;
    uint32_t exe_time;

    while(cliKeepLoop())
    {
      pre_time = millis();
      if (ak4183IsReady())
      {
        if (ak4183ReadAdc(&adc_info) == true)
        {
          exe_time = millis()-pre_time;

          cliPrintf("x_adc : %d , y_adc : %d,  %3d ms\n", adc_info.x_adc, adc_info.y_adc, exe_time);
        }
        else
        {
          cliPrintf("ak4183GetAdc() Fail\n");
          break;
        }
      }
      delay(10);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    ak4183_info_t info;
    uint32_t pre_time;
    uint32_t exe_time;

    while(cliKeepLoop())
    {
      pre_time = millis();
      if (ak4183GetInfo(&info) == true)
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
        cliPrintf("ak4183GetInfo() Fail\n");
        break;
      }
      delay(10);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "gui"))
  {
    ak4183_info_t info;
    ak4183_info_t info_pre;

    info.count = 0;
    info_pre.count = 0;
    cliGui()->initScreen(80, 24);

    while(cliKeepLoop())
    {
      cliGui()->drawBox(0, 0, 480/10 + 1, 320/20 + 1, "");

      if (ak4183GetInfo(&info) == true)
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


  if (args->argc == 1 && args->isStr(0, "cali"))
  {
		if(calculate_calibration_coefficient(5, x_scr_ref, y_scr_ref, x_buf, y_buf) == 0)
    {
      cliPrintf("no cali\n");
    }
    else
    {
      cliPrintf("cali ok\n");
    }


    ret = true;
  }




  if (ret == false)
  {
    cliPrintf("ak4183 info\n");
    cliPrintf("ak4183 get adc\n");
    cliPrintf("ak4183 get info\n");
    cliPrintf("ak4183 gui\n");
  }
}

#endif
