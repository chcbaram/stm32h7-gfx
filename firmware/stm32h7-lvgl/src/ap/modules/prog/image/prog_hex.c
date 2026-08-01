/*
 * prog_hex.c
 *
 *  Intel HEX 스트리밍 리더
 *
 *  파일을 앞으로만 훑는다. 페이지가 낮은 주소부터 요청되는 걸 전제로 하고,
 *  그 전제가 깨지는 파일(주소 역행)은 열 때 거부한다.
 *
 *  f_gets 를 쓰지 않는다. FatFs 의 f_gets 는 한 글자씩 f_read 를 부르기 때문에
 *  800KB 짜리 hex 에서는 그것만으로 수십 초가 나온다. 512 바이트씩 읽어 직접
 *  줄을 끊는다.
 */

#include "prog/image/prog_hex.h"


#ifdef _USE_HW_SWD


typedef struct
{
  uint32_t addr;
  uint32_t len;
  uint8_t  type;
  uint8_t  data[HEX_DATA_MAX + 1];   // +1 은 마지막에 딸려 들어오는 체크섬 자리
} hex_rec_t;


static bool hexGetLine(hex_t *p_hex, char *p_line, uint32_t max);
static bool hexParse(const char *p_line, hex_rec_t *p_rec);
static bool hexNextData(hex_t *p_hex);
static int  hexNibble(char c);


// ----------------------------------------------------------------- 열기/닫기

bool hexOpen(hex_t *p_hex, const char *path)
{
  char      line[HEX_LINE_MAX];
  hex_rec_t rec;
  uint32_t  base = 0;
  uint32_t  prev_end = 0;
  bool      first = true;

  if (p_hex == NULL || path == NULL) return false;

  memset(p_hex, 0, sizeof(hex_t));
  p_hex->lo = 0xFFFFFFFF;

  if (f_open(&p_hex->file, path, FA_READ) != FR_OK) return false;
  p_hex->is_open = true;

  /* 전체 스캔. 체크섬과 주소 순서를 여기서 다 본다 — 지우기 시작한 뒤에
     파일이 이상하다는 걸 알게 되면 손쓸 방법이 없다. */
  while (hexGetLine(p_hex, line, sizeof(line)))
  {
    if (line[0] == 0) continue;

    if (hexParse(line, &rec) == false)
    {
      hexClose(p_hex);
      return false;
    }
    p_hex->rec_cnt++;

    switch (rec.type)
    {
      case HEX_REC_DATA:
      {
        uint32_t addr = base + rec.addr;

        if (first == false && addr < prev_end)
        {
          // 주소가 뒤로 갔다. 앞으로만 훑는 구조라 처리할 수 없다.
          hexClose(p_hex);
          return false;
        }
        if (addr < p_hex->lo)            p_hex->lo = addr;
        if (addr + rec.len > p_hex->hi)  p_hex->hi = addr + rec.len;

        p_hex->data_bytes += rec.len;
        prev_end = addr + rec.len;
        first    = false;
        break;
      }

      case HEX_REC_EXT_SEG:
        base = ((uint32_t)rec.data[0] << 8 | rec.data[1]) << 4;
        break;

      case HEX_REC_EXT_LIN:
        base = ((uint32_t)rec.data[0] << 8 | rec.data[1]) << 16;
        break;

      case HEX_REC_START_SEG:
      case HEX_REC_START_LIN:
        p_hex->entry     = ((uint32_t)rec.data[0] << 24) | ((uint32_t)rec.data[1] << 16) |
                           ((uint32_t)rec.data[2] << 8)  |  (uint32_t)rec.data[3];
        p_hex->has_entry = true;
        break;

      case HEX_REC_EOF:
        break;

      default:
        break;
    }

    if (rec.type == HEX_REC_EOF) break;
  }

  if (p_hex->lo == 0xFFFFFFFF || p_hex->hi <= p_hex->lo)
  {
    hexClose(p_hex);
    return false;
  }

  return hexRewind(p_hex);
}

void hexClose(hex_t *p_hex)
{
  if (p_hex != NULL && p_hex->is_open)
  {
    f_close(&p_hex->file);
    p_hex->is_open = false;
  }
}


// ----------------------------------------------------------------- 공개

bool hexIsHexFile(const char *path)
{
  FIL  file;
  char c  = 0;
  UINT br = 0;
  bool ret;

  if (f_open(&file, path, FA_READ) != FR_OK) return false;
  ret = (f_read(&file, &c, 1, &br) == FR_OK) && (br == 1) && (c == ':');
  f_close(&file);

  return ret;
}

bool hexRewind(hex_t *p_hex)
{
  if (p_hex == NULL || p_hex->is_open == false) return false;

  if (f_lseek(&p_hex->file, 0) != FR_OK) return false;

  p_hex->buf_len   = 0;
  p_hex->buf_pos   = 0;
  p_hex->base      = 0;
  p_hex->rec_valid = false;
  p_hex->at_eof    = false;
  return true;
}

bool hexFill(hex_t *p_hex, uint32_t addr, uint8_t *p_buf, uint32_t len, uint8_t empty)
{
  uint32_t win_end = addr + len;

  if (p_hex == NULL || p_hex->is_open == false) return false;

  memset(p_buf, empty, len);

  while (1)
  {
    uint32_t rec_end;
    uint32_t lo;
    uint32_t hi;

    if (p_hex->rec_valid == false)
    {
      if (p_hex->at_eof) break;
      if (hexNextData(p_hex) == false) break;   // EOF 이거나 파싱 실패
    }

    rec_end = p_hex->rec_addr + p_hex->rec_len;

    // 창보다 완전히 앞이면 버린다 (hexOpen 이 역행을 거부했으므로 첫 창 앞뿐)
    if (rec_end <= addr)
    {
      p_hex->rec_valid = false;
      continue;
    }

    // 창보다 뒤면 다음 창을 위해 물려둔다
    if (p_hex->rec_addr >= win_end) break;

    lo = (p_hex->rec_addr > addr) ? p_hex->rec_addr : addr;
    hi = (rec_end < win_end) ? rec_end : win_end;

    memcpy(&p_buf[lo - addr], &p_hex->rec_data[lo - p_hex->rec_addr], hi - lo);

    // 창을 넘겨 걸치면 물려두고, 다 소비했으면 버린다
    if (rec_end > win_end) break;
    p_hex->rec_valid = false;
  }

  return true;
}


// ----------------------------------------------------------------- 내부

/* 다음 데이터 레코드를 rec_* 에 채운다. 그 사이의 02/04 는 base 에 반영하고
   05/03 은 무시한다 (열 때 이미 읽었다). */
static bool hexNextData(hex_t *p_hex)
{
  char      line[HEX_LINE_MAX];
  hex_rec_t rec;

  while (hexGetLine(p_hex, line, sizeof(line)))
  {
    if (line[0] == 0)                    continue;
    if (hexParse(line, &rec) == false)   return false;

    switch (rec.type)
    {
      case HEX_REC_DATA:
        p_hex->rec_addr = p_hex->base + rec.addr;
        p_hex->rec_len  = rec.len;
        memcpy(p_hex->rec_data, rec.data, rec.len);
        p_hex->rec_valid = true;
        return true;

      case HEX_REC_EXT_SEG:
        p_hex->base = ((uint32_t)rec.data[0] << 8 | rec.data[1]) << 4;
        break;

      case HEX_REC_EXT_LIN:
        p_hex->base = ((uint32_t)rec.data[0] << 8 | rec.data[1]) << 16;
        break;

      case HEX_REC_EOF:
        p_hex->at_eof = true;
        return false;

      default:
        break;
    }
  }

  p_hex->at_eof = true;
  return false;
}

/* 512 바이트씩 읽어 한 줄을 끊는다. CR/LF 는 버리고 널로 끝낸다.
   줄이 max 를 넘으면 넘치는 만큼 버려서 뒤의 파싱이 체크섬으로 걸러내게 한다. */
static bool hexGetLine(hex_t *p_hex, char *p_line, uint32_t max)
{
  uint32_t n = 0;
  bool     got = false;

  while (1)
  {
    if (p_hex->buf_pos >= p_hex->buf_len)
    {
      UINT br = 0;

      if (f_read(&p_hex->file, p_hex->buf, HEX_BUF_SIZE, &br) != FR_OK) return false;
      if (br == 0) break;

      p_hex->buf_len = br;
      p_hex->buf_pos = 0;
    }

    while (p_hex->buf_pos < p_hex->buf_len)
    {
      char c = (char)p_hex->buf[p_hex->buf_pos++];

      if (c == '\n' || c == '\r')
      {
        if (got)
        {
          p_line[n] = 0;
          return true;
        }
        continue;                       // 빈 줄은 그냥 넘긴다
      }
      got = true;
      if (n + 1 < max) p_line[n++] = c;
    }
  }

  p_line[n] = 0;
  return got;
}

/* :LLAAAATT[DD..]CC 를 뜯는다. 체크섬은 전 바이트의 합이 0 이어야 한다.
   SD 읽기가 조용히 깨진 hex 와 실제로 나쁜 플래시는 증상이 같아서, 여기서
   걸러내지 않으면 원인을 플래시 쪽에서 찾게 된다. */
static bool hexParse(const char *p_line, hex_rec_t *p_rec)
{
  uint8_t  sum = 0;
  uint32_t cnt;

  if (p_line[0] != ':') return false;

  // ":" 뒤는 전부 16진 바이트 쌍이어야 한다. 최소 LL AAAA TT CC = 5 바이트
  for (cnt = 0; p_line[1 + cnt * 2] != 0; cnt++)
  {
    int h = hexNibble(p_line[1 + cnt * 2]);
    int l = hexNibble(p_line[2 + cnt * 2]);

    if (h < 0 || l < 0) return false;
    if (cnt > 4 + HEX_DATA_MAX) return false;   // LL 최대 255 + 헤더4 + 체크섬1

    if (cnt == 0) p_rec->len         = (uint32_t)(h << 4 | l);
    if (cnt == 1) p_rec->addr        = (uint32_t)(h << 4 | l) << 8;
    if (cnt == 2) p_rec->addr       |= (uint32_t)(h << 4 | l);
    if (cnt == 3) p_rec->type        = (uint8_t)(h << 4 | l);
    if (cnt >= 4) p_rec->data[cnt-4] = (uint8_t)(h << 4 | l);

    sum += (uint8_t)(h << 4 | l);
  }

  if (cnt < 5)                return false;
  if (cnt != p_rec->len + 5)  return false;   // LL 과 실제 길이가 다르다
  if (sum != 0)               return false;   // 체크섬 (마지막 CC 포함해 0)

  return true;
}

static int hexNibble(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}


#endif
