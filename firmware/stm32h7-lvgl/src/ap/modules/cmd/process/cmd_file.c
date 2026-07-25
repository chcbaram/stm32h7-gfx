#include "cmd_file.h"


#ifdef _USE_HW_CMD
#include "files.h"
#include "fs.h"
#include "ff_gen_drv.h"


#define FILE_CMD_INFO       0x0100
#define FILE_CMD_BEGIN      0x0101
#define FILE_CMD_WRITE      0x0102
#define FILE_CMD_END        0x0103
#define FILE_CMD_DEL        0x0104
#define FILE_CMD_MKDIR      0x0105


static uint16_t cmdFileBegin(uint8_t *p_data, uint16_t length);
static uint16_t cmdFileWrite(uint8_t *p_data, uint16_t length);
static uint16_t cmdFileEnd(uint8_t *p_data, uint16_t length);
static uint16_t cmdFileDel(uint8_t *p_data, uint16_t length);
static uint16_t cmdFileMkdir(uint8_t *p_data, uint16_t length);
static void     cmdFileClose(void);

static cmd_file_info_t file_info;
static FILE *fp_sd = NULL;
static fs_t  fp_fs;




bool cmdFileInit(void)
{
  memset(&file_info, 0, sizeof(file_info));
  file_info.is_begin = false;
  return true;
}

bool cmdFileIsBusy(void)
{
  return file_info.is_begin;
}

void cmdFileGetInfo(cmd_file_info_t *p_info)
{
  *p_info = file_info;
}

bool cmdFileProcess(cmd_t *p_cmd)
{
  bool ret = true;
  uint16_t err_code = CMD_OK;
  cmd_packet_t *p_packet = &p_cmd->packet;
  uint8_t  resp[16];
  uint16_t resp_len = 0;


  switch(p_packet->cmd)
  {
    case FILE_CMD_INFO:
    {
      int32_t fs_free = fsGetFree();

      resp[resp_len++] = (CMD_MAX_DATA_LENGTH >> 0) & 0xFF;
      resp[resp_len++] = (CMD_MAX_DATA_LENGTH >> 8) & 0xFF;
      resp[resp_len++] = sdIsDetected() ? 1 : 0;
      resp[resp_len++] = (fs_free >>  0) & 0xFF;
      resp[resp_len++] = (fs_free >>  8) & 0xFF;
      resp[resp_len++] = (fs_free >> 16) & 0xFF;
      resp[resp_len++] = (fs_free >> 24) & 0xFF;
      break;
    }

    case FILE_CMD_BEGIN:
      err_code = cmdFileBegin(p_packet->data, p_packet->length);
      break;

    case FILE_CMD_WRITE:
      err_code = cmdFileWrite(p_packet->data, p_packet->length);
      break;

    case FILE_CMD_END:
      err_code = cmdFileEnd(p_packet->data, p_packet->length);
      break;

    case FILE_CMD_DEL:
      err_code = cmdFileDel(p_packet->data, p_packet->length);
      break;

    case FILE_CMD_MKDIR:
      err_code = cmdFileMkdir(p_packet->data, p_packet->length);
      break;

    default:
      ret = false;
      break;
  }

  if (ret == true)
  {
    cmdSendResp(p_cmd, p_packet->cmd, err_code, resp, resp_len);
  }

  return ret;
}

/* data : [drive(1)][size(4)][name(z-string)] */
uint16_t cmdFileBegin(uint8_t *p_data, uint16_t length)
{
  uint8_t  drive;
  uint32_t size;
  uint16_t name_len;


  if (length < 6)
    return ERR_FILE_SIZE;

  /* 이전 전송이 남아있으면 정리한다. */
  cmdFileClose();

  drive = p_data[0];
  size  = (p_data[1] <<  0) | (p_data[2] <<  8) |
          (p_data[3] << 16) | (p_data[4] << 24);

  name_len = length - 5;
  if (name_len >= sizeof(file_info.name))
    return ERR_FILE_NAME;

  memcpy(file_info.name, &p_data[5], name_len);
  file_info.name[name_len] = 0;

  if (drive == FILE_DRIVE_SD)
  {
    if (sdIsDetected() != true)
      return ERR_FILE_OPEN;

    fp_sd = fopen(file_info.name, "w");
    if (fp_sd == NULL)
      return ERR_FILE_OPEN;
  }
  else if (drive == FILE_DRIVE_FS)
  {
    /* littlefs 는 truncate 가 없어 지우고 새로 만든다. */
    if (fsIsExist(file_info.name) == true)
    {
      fsFileDel(file_info.name);
    }
    if (fsFileOpen(&fp_fs, file_info.name) != true)
      return ERR_FILE_OPEN;
  }
  else
  {
    return ERR_FILE_OPEN;
  }

  file_info.drive     = drive;
  file_info.size      = size;
  file_info.recv_size = 0;
  file_info.crc       = 0;
  file_info.is_begin  = true;

  logPrintf("[  ] file begin : %s (%d bytes)\n", file_info.name, (int)size);
  return CMD_OK;
}

/* data : [offset(4)][bytes...] */
uint16_t cmdFileWrite(uint8_t *p_data, uint16_t length)
{
  uint32_t offset;
  uint16_t data_len;
  uint8_t *p_buf;


  if (file_info.is_begin != true)
    return ERR_FILE_NOT_BEGIN;

  if (length < 4)
    return ERR_FILE_SIZE;

  offset   = (p_data[0] <<  0) | (p_data[1] <<  8) |
             (p_data[2] << 16) | (p_data[3] << 24);
  data_len = length - 4;
  p_buf    = &p_data[4];

  /* 순차 전송만 지원한다. 어긋나면 재전송을 유도한다. */
  if (offset != file_info.recv_size)
    return ERR_FILE_SIZE;

  if (file_info.recv_size + data_len > file_info.size)
    return ERR_FILE_SIZE;

  if (file_info.drive == FILE_DRIVE_SD)
  {
    if (fwrite(p_buf, 1, data_len, fp_sd) != data_len)
      return ERR_FILE_WRITE;
  }
  else
  {
    if (fsFileWrite(&fp_fs, p_buf, data_len) != (int32_t)data_len)
      return ERR_FILE_WRITE;
  }

  for (int i = 0; i < data_len; i++)
  {
    utilUpdateCrc(&file_info.crc, p_buf[i]);
  }
  file_info.recv_size += data_len;

  return CMD_OK;
}

/* data : [crc(2)] */
uint16_t cmdFileEnd(uint8_t *p_data, uint16_t length)
{
  uint16_t crc_host;
  uint16_t err_code = CMD_OK;


  if (file_info.is_begin != true)
    return ERR_FILE_NOT_BEGIN;

  if (length < 2)
    return ERR_FILE_SIZE;

  crc_host = (p_data[0] << 0) | (p_data[1] << 8);

  if (file_info.recv_size != file_info.size)
  {
    err_code = ERR_FILE_SIZE;
  }
  else if (crc_host != file_info.crc)
  {
    err_code = ERR_FILE_CRC;
  }

  cmdFileClose();

  logPrintf("[%s] file end : %s (%d bytes)\n",
            err_code == CMD_OK ? "OK":"NG", file_info.name, (int)file_info.recv_size);
  return err_code;
}

/* data : [drive(1)][name(z-string)] */
uint16_t cmdFileDel(uint8_t *p_data, uint16_t length)
{
  char name[128];
  uint16_t name_len;


  if (length < 2)
    return ERR_FILE_NAME;

  name_len = length - 1;
  if (name_len >= sizeof(name))
    return ERR_FILE_NAME;

  memcpy(name, &p_data[1], name_len);
  name[name_len] = 0;

  if (p_data[0] == FILE_DRIVE_FS)
  {
    if (fsFileDel(name) != true)
      return ERR_FILE_DEL;
  }
  else
  {
    if (f_unlink(name) != FR_OK)
      return ERR_FILE_DEL;
  }

  return CMD_OK;
}

/* data : [drive(1)][path(z-string)] */
uint16_t cmdFileMkdir(uint8_t *p_data, uint16_t length)
{
  char path[128];
  uint16_t path_len;


  if (length < 2)
    return ERR_FILE_NAME;

  path_len = length - 1;
  if (path_len >= sizeof(path))
    return ERR_FILE_NAME;

  memcpy(path, &p_data[1], path_len);
  path[path_len] = 0;

  if (p_data[0] == FILE_DRIVE_FS)
  {
    if (fsMakeDir(path) != true)
      return ERR_FILE_WRITE;
  }
  else
  {
    if (f_mkdir(path) != FR_OK)
      return ERR_FILE_WRITE;
  }

  return CMD_OK;
}

void cmdFileClose(void)
{
  if (file_info.is_begin != true)
    return;

  if (file_info.drive == FILE_DRIVE_SD)
  {
    if (fp_sd != NULL)
    {
      fclose(fp_sd);
      fp_sd = NULL;
    }
  }
  else
  {
    fsFileClose(&fp_fs);
  }

  file_info.is_begin = false;
}

#endif
