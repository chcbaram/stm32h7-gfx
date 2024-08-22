#ifndef AK4183_H_
#define AK4183_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_AK4183


#define AK4183_MAX_TOUCH_POINT             1
#define AK4183_EEPROM_MAGIC_NUMBER   0x34313833 // "4183"
#define AK4183_READ_ADC_CNT     50

#if LCD_MODEL_7_0_800x480_RTP
    #define AK4183_COORDINATE_X1  90
    #define AK4183_COORDINATE_X2  90
    #define AK4183_COORDINATE_X3  700
    #define AK4183_COORDINATE_X4  700
    #define AK4183_COORDINATE_X5  400

    #define AK4183_COORDINATE_Y1  90
    #define AK4183_COORDINATE_Y2  400
    #define AK4183_COORDINATE_Y3  400
    #define AK4183_COORDINATE_Y4  90
    #define AK4183_COORDINATE_Y5  250

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
#elif LCD_MODEL_4_3_480x272_RTP
    #define AK4183_COORDINATE_X1  42
    #define AK4183_COORDINATE_X2  42
    #define AK4183_COORDINATE_X3  438
    #define AK4183_COORDINATE_X4  438
    #define AK4183_COORDINATE_X5  240

    #define AK4183_COORDINATE_Y1  42
    #define AK4183_COORDINATE_Y2  240
    #define AK4183_COORDINATE_Y3  240
    #define AK4183_COORDINATE_Y4  42
    #define AK4183_COORDINATE_Y5  136

    #define AK4183_DEFAULT_ADC_X1   483
    #define AK4183_DEFAULT_ADC_X2   480
    #define AK4183_DEFAULT_ADC_X3   3621
    #define AK4183_DEFAULT_ADC_X4   3612
    #define AK4183_DEFAULT_ADC_X5   2050

    #define AK4183_DEFAULT_ADC_Y1   742
    #define AK4183_DEFAULT_ADC_Y2   3286
    #define AK4183_DEFAULT_ADC_Y3   3305
    #define AK4183_DEFAULT_ADC_Y4   784
    #define AK4183_DEFAULT_ADC_Y5   1949
#endif

typedef struct
{
  uint8_t  id;
  uint8_t  event;
  uint16_t x;
  uint16_t y;
  uint8_t  weight;
  uint8_t  area;
} ak4183_point_t;

typedef struct
{
  uint8_t gest_id;
  uint8_t count;
  ak4183_point_t point[AK4183_MAX_TOUCH_POINT];
} ak4183_info_t;

typedef enum
{
	TCH_POINT_1,
	TCH_POINT_2,
	TCH_POINT_3,
	TCH_POINT_4,
	TCH_POINT_5
} RtpCalibrationStep_t;

typedef struct
{
  uint32_t x_adc[5];          // 20
  uint32_t y_adc[5];          // 20
  uint32_t tch_magic_number;  // 4
} ak4183_data_t; // 44

typedef struct
{
  uint32_t x_adc;
  uint32_t y_adc;
} ak4183_adc_t;



bool ak4183Init(void);
bool ak4183GetInfo(ak4183_info_t *p_info);
uint16_t ak4183GetWidth(void);
uint16_t ak4183GetHeight(void);

bool ak4183ReadAdc(ak4183_adc_t *p_adc);
bool ak4183IsCaliResultErr(ak4183_data_t *p_data);
bool ak4183SaveCaliData(ak4183_data_t* p_data);
bool ak4183touchDataWrite(ak4183_data_t* p_data);
bool ak4183touchDataRead(ak4183_data_t* p_data);
bool ak4183SetDefault(void);

#endif

#ifdef __cplusplus
 }
#endif

#endif
