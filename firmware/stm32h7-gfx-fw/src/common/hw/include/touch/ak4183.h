#ifndef AK4183_H_
#define AK4183_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_AK4183


#define AK4183_MAX_TOUCH_POINT             1
#define PRESS_TIME          3 // Second

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

typedef struct
{
  uint32_t x_adc[5];
  uint32_t y_adc[5];
  uint8_t state;
} tch_cali_info_t;

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
} ak4183_cali_t; // 44

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
bool ak4183IsCaliResultErr(tch_cali_info_t tch_info);
bool ak4183CalibrationProc(int16_t x, int16_t y);
bool ak4183SaveCaliData(tch_cali_info_t tch_info);
bool ak4183touchDataWrite(ak4183_cali_t* p_data);
bool ak4183touchDataRead(ak4183_cali_t* p_data);
#endif

#ifdef __cplusplus
 }
#endif

#endif
