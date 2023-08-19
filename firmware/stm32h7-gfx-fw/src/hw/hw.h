#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#include "led.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "button.h"
#include "qbuffer.h"
#include "swtimer.h"
#include "gpio.h"
#include "flash.h"
#include "fault.h"
#include "qspi.h"
#include "fs.h"
#include "sd.h"
#include "fatfs.h"
#include "i2c.h"
#include "i2s.h"
#include "sai.h"
#include "spi.h"
#include "lcd.h"
#include "pwm.h"
#include "nvs.h"
#include "usb.h"
#include "cdc.h"
#include "rtc.h"
#include "reset.h"
#include "cmd.h"
#include "util.h"
#include "eeprom.h"
#include "fmc.h"
#include "sdram.h"
#include "spi_flash.h"
#include "touch.h"
#include "ltdc.h"
#include "pdm.h"
#include "mem.h"

bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif