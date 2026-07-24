#include "cmd_screen.h"


#ifdef _USE_HW_CMD
#ifdef _USE_HW_LCD
#include "ltdc.h"
#include "mem.h"


#define SCREEN_CMD_INFO       0x0200
#define SCREEN_CMD_CAPTURE    0x0201
#define SCREEN_CMD_READ       0x0202
#define SCREEN_CMD_END        0x0203

#define SCREEN_FMT_RGB565     0
#define SCREEN_FMT_RLE565     1


typedef struct
{
  bool     is_capture;
  uint8_t  format;
  uint8_t *p_buf;
  uint32_t length;
} screen_info_t;


static uint16_t screenCapture(uint8_t *p_data, uint16_t length);
static uint16_t screenRead(cmd_t *p_cmd, uint8_t *p_data, uint16_t length);
static void     screenEnd(void);
static uint32_t screenEncodeRle(const uint16_t *p_src, uint32_t pixel_cnt,
                                uint8_t *p_dst, uint32_t dst_max);

static screen_info_t screen_info;




bool cmdScreenInit(void)
{
  memset(&screen_info, 0, sizeof(screen_info));
  return true;
}

bool cmdScreenProcess(cmd_t *p_cmd)
{
  bool ret = true;
  uint16_t err_code = CMD_OK;
  cmd_packet_t *p_packet = &p_cmd->packet;
  uint8_t  resp[16];
  uint16_t resp_len = 0;
  bool     resp_sent = false;


  switch(p_packet->cmd)
  {
    case SCREEN_CMD_INFO:
      resp[resp_len++] = (LCD_WIDTH  >> 0) & 0xFF;
      resp[resp_len++] = (LCD_WIDTH  >> 8) & 0xFF;
      resp[resp_len++] = (LCD_HEIGHT >> 0) & 0xFF;
      resp[resp_len++] = (LCD_HEIGHT >> 8) & 0xFF;
      resp[resp_len++] = 16;                      /* bpp        */
      resp[resp_len++] = SCREEN_FMT_RLE565;       /* 최대 지원  */
      break;

    case SCREEN_CMD_CAPTURE:
      err_code = screenCapture(p_packet->data, p_packet->length);
      if (err_code == CMD_OK)
      {
        resp[resp_len++] = screen_info.format;
        resp[resp_len++] = (screen_info.length >>  0) & 0xFF;
        resp[resp_len++] = (screen_info.length >>  8) & 0xFF;
        resp[resp_len++] = (screen_info.length >> 16) & 0xFF;
        resp[resp_len++] = (screen_info.length >> 24) & 0xFF;
      }
      break;

    case SCREEN_CMD_READ:
      err_code  = screenRead(p_cmd, p_packet->data, p_packet->length);
      resp_sent = (err_code == CMD_OK);
      break;

    case SCREEN_CMD_END:
      screenEnd();
      break;

    default:
      ret = false;
      break;
  }

  if (ret == true && resp_sent == false)
  {
    cmdSendResp(p_cmd, p_packet->cmd, err_code, resp, resp_len);
  }

  return ret;
}

/* data : [format(1)]
 *
 * 표시중인 프레임버퍼를 통째로 떠서 따로 보관한다.
 * 전송 도중에 화면이 갱신되어도 찢어지지 않게 하기 위함이다.
 */
uint16_t screenCapture(uint8_t *p_data, uint16_t length)
{
  const uint16_t *p_fb;
  uint32_t pixel_cnt = LCD_WIDTH * LCD_HEIGHT;
  uint32_t raw_len   = pixel_cnt * 2;
  uint8_t  format    = SCREEN_FMT_RGB565;


  if (length >= 1)
    format = p_data[0];

  screenEnd();

  p_fb = (const uint16_t *)ltdcGetCurrentFrameBuffer();
  if (p_fb == NULL)
    return ERR_NULL;

  screen_info.p_buf = (uint8_t *)memMalloc(raw_len);
  if (screen_info.p_buf == NULL)
    return ERR_MEMORY;

  if (format == SCREEN_FMT_RLE565)
  {
    uint32_t rle_len;

    rle_len = screenEncodeRle(p_fb, pixel_cnt, screen_info.p_buf, raw_len);
    if (rle_len > 0)
    {
      screen_info.format = SCREEN_FMT_RLE565;
      screen_info.length = rle_len;
    }
    else
    {
      /* 압축이 원본보다 커지면 그냥 원본을 보낸다. */
      memcpy(screen_info.p_buf, p_fb, raw_len);
      screen_info.format = SCREEN_FMT_RGB565;
      screen_info.length = raw_len;
    }
  }
  else
  {
    memcpy(screen_info.p_buf, p_fb, raw_len);
    screen_info.format = SCREEN_FMT_RGB565;
    screen_info.length = raw_len;
  }

  screen_info.is_capture = true;
  return CMD_OK;
}

/* data : [offset(4)][len(2)] -> 응답 데이터로 픽셀을 실어 보낸다. */
uint16_t screenRead(cmd_t *p_cmd, uint8_t *p_data, uint16_t length)
{
  uint32_t offset;
  uint16_t req_len;


  if (screen_info.is_capture != true)
    return ERR_FILE_NOT_BEGIN;

  if (length < 6)
    return ERR_FILE_SIZE;

  offset  = (p_data[0] <<  0) | (p_data[1] <<  8) |
            (p_data[2] << 16) | (p_data[3] << 24);
  req_len = (p_data[4] << 0) | (p_data[5] << 8);

  if (offset >= screen_info.length)
    return ERR_FILE_SIZE;

  if (offset + req_len > screen_info.length)
    req_len = screen_info.length - offset;

  if (req_len > CMD_MAX_DATA_LENGTH)
    req_len = CMD_MAX_DATA_LENGTH;

  cmdSendResp(p_cmd, SCREEN_CMD_READ, CMD_OK, &screen_info.p_buf[offset], req_len);
  return CMD_OK;
}

void screenEnd(void)
{
  if (screen_info.p_buf != NULL)
  {
    memFree(screen_info.p_buf);
    screen_info.p_buf = NULL;
  }
  screen_info.is_capture = false;
  screen_info.length     = 0;
}

/* RGB565 런렝스 압축
 *
 *   [count(1)][pixel_lo][pixel_hi]   count 는 1..255
 *
 * UI 화면은 단색 면이 많아 사진용 압축보다 이쪽이 훨씬 잘 줄고 코드도 짧다.
 * 원본보다 커지면 0 을 돌려준다.
 */
uint32_t screenEncodeRle(const uint16_t *p_src, uint32_t pixel_cnt,
                         uint8_t *p_dst, uint32_t dst_max)
{
  uint32_t src_i = 0;
  uint32_t dst_i = 0;


  while (src_i < pixel_cnt)
  {
    uint16_t color = p_src[src_i];
    uint32_t run   = 1;

    while (src_i + run < pixel_cnt && run < 255 && p_src[src_i + run] == color)
    {
      run++;
    }

    if (dst_i + 3 > dst_max)
      return 0;

    p_dst[dst_i++] = (uint8_t)run;
    p_dst[dst_i++] = (color >> 0) & 0xFF;
    p_dst[dst_i++] = (color >> 8) & 0xFF;

    src_i += run;
  }

  return dst_i;
}

#endif
#endif
