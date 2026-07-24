#ifndef CMD_FILE_H_
#define CMD_FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

/* 전송 대상 저장매체 */
enum
{
  FILE_DRIVE_SD = 0,      /* SD 카드 (FatFs)        */
  FILE_DRIVE_FS = 1,      /* SPI Flash (littlefs)   */
};

typedef struct
{
  bool     is_begin;
  uint8_t  drive;
  char     name[128];
  uint32_t size;
  uint32_t recv_size;
  uint16_t crc;
} cmd_file_info_t;


bool cmdFileInit(void);
bool cmdFileIsBusy(void);
bool cmdFileProcess(cmd_t *p_cmd);
void cmdFileGetInfo(cmd_file_info_t *p_info);

#endif

#ifdef __cplusplus
}
#endif

#endif
