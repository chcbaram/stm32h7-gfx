#ifndef GSL1680_H_
#define GSL1680_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_GSL1680

#define GSL1680_MAX_CONTACTS  HW_TOUCH_MAX_CH

typedef struct
{
  uint8_t  id;
  uint8_t  event;
  uint16_t x;
  uint16_t y;
  uint8_t  weight;
  uint8_t  area;
} gsl1680_point_t;

typedef struct
{
  uint8_t count;
  gsl1680_point_t point[GSL1680_MAX_CONTACTS];
} gsl1680_info_t;

bool gsl1680Init(void);
bool gsl1680GetInfo(gsl1680_info_t *p_info);
uint16_t gsl1680GetWidth(void);
uint16_t gsl1680GetHeight(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
