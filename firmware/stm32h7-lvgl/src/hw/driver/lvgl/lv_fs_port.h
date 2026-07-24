#ifndef LV_FS_PORT_H
#define LV_FS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


/* LVGL 파일 경로 규칙
 *
 *   "S:/ui/logo.bin"   SD 카드    (FatFs,    upload.py -d sd)
 *   "F:/ui/logo.bin"   SPI Flash  (littlefs, upload.py -d spi)
 */
#define LV_FS_DRIVE_SD    "S:"
#define LV_FS_DRIVE_SPI   "F:"


void lv_fs_port_init(void);


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
