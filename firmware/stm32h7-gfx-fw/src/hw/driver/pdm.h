#ifndef PDM_H_
#define PDM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_PDM


bool pdmInit(void);
bool pdmIsInit(void);


#endif


#ifdef __cplusplus
}
#endif

#endif