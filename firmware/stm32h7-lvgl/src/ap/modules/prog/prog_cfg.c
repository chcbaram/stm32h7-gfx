/*
 * prog_cfg.c
 *
 *  key = value 설정 파서
 *
 *  f_gets 를 쓰지 않는다. FatFs 구현이 한 글자씩 f_read 를 부르기 때문에
 *  줄 수가 조금만 늘어도 비싸진다. prog_hex.c 와 같은 이유, 같은 방식이다.
 */

#include "prog/prog_cfg.h"


#ifdef _USE_HW_SWD


#define CFG_BUF_SIZE    512     // 32 배수 (sd.c 의 캐시 무효화 때문)


typedef struct
{
  FIL      file;
  uint8_t  buf[CFG_BUF_SIZE] __attribute__((aligned(32)));
  uint32_t buf_len;
  uint32_t buf_pos;
} cfg_t;


static bool  cfgGetLine(cfg_t *p_cfg, char *p_line, uint32_t max);
static char *cfgTrim(char *s);


// ----------------------------------------------------------------- 공개

bool cfgParse(const char *path, cfg_cb_t cb, void *ctx)
{
  static cfg_t cfg;                 // FIL + 512B 라 스택에 두기엔 크다
  char  line[CFG_LINE_MAX];
  char  sec[CFG_SEC_MAX] = {0};

  if (path == NULL || cb == NULL) return false;

  if (f_open(&cfg.file, path, FA_READ) != FR_OK) return false;
  cfg.buf_len = 0;
  cfg.buf_pos = 0;

  while (cfgGetLine(&cfg, line, sizeof(line)))
  {
    char *p = cfgTrim(line);
    char *eq;

    if (*p == 0 || *p == '#' || *p == ';') continue;

    if (*p == '[')
    {
      char *end = strchr(p, ']');

      if (end == NULL) continue;    // 닫히지 않은 섹션은 무시한다
      *end = 0;
      snprintf(sec, sizeof(sec), "%s", cfgTrim(p + 1));
      continue;
    }

    eq = strchr(p, '=');
    if (eq == NULL) continue;       // key = value 가 아니면 무시
    *eq = 0;

    {
      char key[CFG_KEY_MAX];
      char val[CFG_VAL_MAX];

      snprintf(key, sizeof(key), "%s", cfgTrim(p));
      snprintf(val, sizeof(val), "%s", cfgTrim(eq + 1));

      if (key[0] == 0) continue;

      if (cb(sec, key, val, ctx) == false) break;   // 찾았으니 그만
    }
  }

  f_close(&cfg.file);
  return true;
}

uint32_t cfgNum(const char *s)
{
  uint32_t v = 0;

  if (s == NULL) return 0;

  while (*s == ' ' || *s == '\t') s++;

  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
  {
    for (s += 2; *s; s++)
    {
      if      (*s >= '0' && *s <= '9') v = (v << 4) | (uint32_t)(*s - '0');
      else if (*s >= 'a' && *s <= 'f') v = (v << 4) | (uint32_t)(*s - 'a' + 10);
      else if (*s >= 'A' && *s <= 'F') v = (v << 4) | (uint32_t)(*s - 'A' + 10);
      else break;
    }
    return v;
  }

  for (; *s >= '0' && *s <= '9'; s++)
  {
    v = v * 10 + (uint32_t)(*s - '0');
  }
  return v;
}


// ----------------------------------------------------------------- 내부

/* 512 바이트씩 읽어 한 줄을 끊는다. CR/LF 는 버리고 널로 끝낸다.
   빈 줄도 그대로 돌려준다 — 위에서 걸러낸다. */
static bool cfgGetLine(cfg_t *p_cfg, char *p_line, uint32_t max)
{
  uint32_t n = 0;

  while (1)
  {
    if (p_cfg->buf_pos >= p_cfg->buf_len)
    {
      UINT br = 0;

      if (f_read(&p_cfg->file, p_cfg->buf, CFG_BUF_SIZE, &br) != FR_OK) return false;
      if (br == 0)
      {
        p_line[n] = 0;
        return (n > 0);           // 마지막 줄에 개행이 없는 경우
      }
      p_cfg->buf_len = br;
      p_cfg->buf_pos = 0;
    }

    while (p_cfg->buf_pos < p_cfg->buf_len)
    {
      char c = (char)p_cfg->buf[p_cfg->buf_pos++];

      if (c == '\n')
      {
        p_line[n] = 0;
        return true;
      }
      if (c == '\r') continue;
      if (n + 1 < max) p_line[n++] = c;
    }
  }
}

static char *cfgTrim(char *s)
{
  char *end;

  while (*s == ' ' || *s == '\t') s++;
  if (*s == 0) return s;

  end = s + strlen(s) - 1;
  while (end > s && (*end == ' ' || *end == '\t')) *end-- = 0;

  return s;
}


#endif
