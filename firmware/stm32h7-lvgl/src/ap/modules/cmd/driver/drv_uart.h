#ifndef DRV_UART_H_
#define DRV_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

bool drvUartInit(cmd_driver_t *p_driver, uint8_t ch, uint32_t baud);

#endif

#ifdef __cplusplus
}
#endif

#endif
