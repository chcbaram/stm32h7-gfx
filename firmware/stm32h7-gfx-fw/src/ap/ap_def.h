#ifndef AP_DEF_H_
#define AP_DEF_H_


#include "hw.h"


#define _USE_AP_PICOVOICE       0
#define _USE_AP_TOUCHGFX        1

#if _USE_AP_TOUCHGFX

#define PRESS_TIME          3 // Second
typedef enum
{
    CALIBRATION_START,
    TCH_POINT_1,
    TCH_POINT_2,
    TCH_POINT_3,
    TCH_POINT_4,
    TCH_POINT_5,
    CALIBRATION_END
} RtpCalibrationStep_t;

#endif

#endif