#ifndef AK4183_H_
#define AK4183_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_AK4183


#define AK4183_MAX_TOUCH_POINT             1


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
    R_TOUCH_CALI_IDLE,
    R_TOUCH_CALI_1,
    R_TOUCH_CALI_2,
    R_TOUCH_CALI_3,
    R_TOUCH_CALI_4,
    R_TOUCH_CALI_5,
    R_TOUCH_CALI_END
} CalibrationStep_t;

bool ak4183Init(void);
bool ak4183GetInfo(ak4183_info_t *p_info);
uint16_t ak4183GetWidth(void);
uint16_t ak4183GetHeight(void);

bool ak4183IsCaliResultErr(tch_cali_info_t tch_info);
bool ak4183GetAdc(uint16_t* x_adc, uint16_t* y_adc);
bool ak4183SaveCaliData(tch_cali_info_t tch_info);

#endif

#ifdef __cplusplus
 }
#endif

#endif
