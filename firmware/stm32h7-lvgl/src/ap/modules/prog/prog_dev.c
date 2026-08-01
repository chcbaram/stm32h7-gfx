/*
 * prog_dev.c
 *
 *  디바이스 DB
 *
 *  항목을 메모리에 쌓지 않고 그때그때 훑는다. 파일이 커져도 .bss 는 그대로다.
 *
 *  자동 판별은 두 번 훑는다.
 *    1) DB 에 등장하는 서로 다른 id_addr 를 모은다
 *    2) 그 주소들을 타깃에서 읽는다 (없는 주소는 FAULT 로 걸러진다)
 *    3) 다시 훑으면서 (읽은값 & id_mask) == id_val 인 항목을 찾는다
 *  "어디를 읽을지 알려면 무슨 칩인지 알아야 한다" 는 닭-달걀 문제를 이렇게 푼다.
 *  후보 주소가 몇 개뿐이라 읽기 서너 번이면 끝난다.
 */

#include "prog/prog_dev.h"
#include "prog/prog_cfg.h"
#include "swd/swd_dap.h"


#ifdef _USE_HW_SWD


typedef void (*dev_entry_cb_t)(const prog_dev_t *p_dev, void *ctx);

typedef struct
{
  prog_dev_t          cur;
  char           cur_sec[CFG_SEC_MAX];
  dev_entry_cb_t cb;
  void          *ctx;
  bool           stop;
} dev_scan_t;

// devFind
typedef struct
{
  const char *name;
  prog_dev_t      *p_out;
  bool        found;
} dev_find_t;

// devDetect 1단계 — 서로 다른 id_addr 모으기
typedef struct
{
  uint32_t addr[DEV_ADDR_MAX];
  uint32_t val[DEV_ADDR_MAX];
  bool     ok[DEV_ADDR_MAX];
  uint32_t cnt;
} dev_addr_t;

// devDetect 2단계 — 맞는 항목 찾기
typedef struct
{
  dev_addr_t *p_addr;
  prog_dev_t      *p_out;
  uint32_t    match;
} dev_match_t;

// devList
typedef struct
{
  bool    (*cb)(const prog_dev_t *, void *);
  void     *ctx;
  uint32_t  cnt;
  bool      stop;
} dev_list_t;


static bool devScanAll(dev_entry_cb_t cb, void *ctx);
static bool devScanFile(const char *path, dev_entry_cb_t cb, void *ctx);
static bool devLineCb(const char *sec, const char *key, const char *val, void *ctx);
static void devFlush(dev_scan_t *p_scan);
static bool devNameEq(const char *a, const char *b);
static void devFindCb(const prog_dev_t *p_dev, void *ctx);
static void devAddrCb(const prog_dev_t *p_dev, void *ctx);
static void devMatchCb(const prog_dev_t *p_dev, void *ctx);
static void devListCb(const prog_dev_t *p_dev, void *ctx);


// ----------------------------------------------------------------- 공개

bool devFind(const char *name, prog_dev_t *p_dev)
{
  dev_find_t f;

  if (name == NULL || p_dev == NULL) return false;

  f.name  = name;
  f.p_out = p_dev;
  f.found = false;

  devScanAll(devFindCb, &f);
  return f.found;
}

swd_err_t devDetect(prog_dev_t *p_dev, uint32_t *p_id)
{
  dev_addr_t  addrs;
  dev_match_t m;

  if (p_dev == NULL) return SWD_ERR_PROTOCOL;

  memset(&addrs, 0, sizeof(addrs));

  // 1) DB 에 나오는 서로 다른 id_addr 를 모은다
  if (devScanAll(devAddrCb, &addrs) == false) return SWD_ERR_PROTOCOL;
  if (addrs.cnt == 0)                         return SWD_ERR_NORESP;

  /* 2) 타깃에서 읽는다. 그 칩에 없는 주소는 FAULT 가 나는데 정상이다 —
        후보를 좁히는 게 목적이라 실패한 건 그냥 빼고 간다. */
  for (uint32_t i = 0; i < addrs.cnt; i++)
  {
    if (addrs.addr[i] == DEV_ID_TARGETID)
    {
      addrs.ok[i] = (swdDapReadTargetId(&addrs.val[i]) == SWD_OK);
    }
    else
    {
      addrs.ok[i] = (swdMemRead32(addrs.addr[i], &addrs.val[i]) == SWD_OK);
    }
  }

  // 3) 다시 훑으면서 맞는 항목을 찾는다
  memset(p_dev, 0, sizeof(prog_dev_t));
  m.p_addr = &addrs;
  m.p_out  = p_dev;
  m.match  = 0;

  devScanAll(devMatchCb, &m);

  if (p_id != NULL)
  {
    *p_id = 0;
    for (uint32_t i = 0; i < addrs.cnt; i++)
    {
      if (addrs.ok[i] && addrs.addr[i] == p_dev->id_addr) { *p_id = addrs.val[i]; break; }
    }
    // 못 찾았으면 처음으로 읽힌 값이라도 보여준다 (진단용)
    if (*p_id == 0)
    {
      for (uint32_t i = 0; i < addrs.cnt; i++)
      {
        if (addrs.ok[i]) { *p_id = addrs.val[i]; break; }
      }
    }
  }

  if (m.match == 0) return SWD_ERR_NORESP;
  if (m.match > 1)  return SWD_ERR_FAULT;    // 이름을 직접 지정해야 한다
  return SWD_OK;
}

uint32_t devList(bool (*cb)(const prog_dev_t *p_dev, void *ctx), void *ctx)
{
  dev_list_t l;

  l.cb   = cb;
  l.ctx  = ctx;
  l.cnt  = 0;
  l.stop = false;

  devScanAll(devListCb, &l);
  return l.cnt;
}


// ----------------------------------------------------------------- 내부

/* /prog/mcu 의 txt 파일을 전부 훑는다. 벤더별로 파일을 쪼개도 되게 하려는 것이라
   파일 이름은 보지 않고 확장자만 본다. */
static bool devScanAll(dev_entry_cb_t cb, void *ctx)
{
  DIR     dir;
  FILINFO fno;
  char    path[288];   // HW_SWD_SD_MCU + / + LFN 최대 255 + 널
  bool    any = false;

  if (f_opendir(&dir, HW_SWD_SD_MCU) != FR_OK) return false;

  while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0)
  {
    const char *ext;

    if (fno.fattrib & AM_DIR) continue;

    ext = strrchr(fno.fname, '.');
    if (ext == NULL || strcasecmp(ext, ".txt") != 0) continue;

    snprintf(path, sizeof(path), "%s/%s", HW_SWD_SD_MCU, fno.fname);
    if (devScanFile(path, cb, ctx)) any = true;
  }

  f_closedir(&dir);
  return any;
}

static bool devScanFile(const char *path, dev_entry_cb_t cb, void *ctx)
{
  dev_scan_t scan;

  memset(&scan, 0, sizeof(scan));
  scan.cb  = cb;
  scan.ctx = ctx;

  if (cfgParse(path, devLineCb, &scan) == false) return false;

  devFlush(&scan);      // 마지막 항목은 파일이 끝나야 나온다
  return true;
}

static bool devLineCb(const char *sec, const char *key, const char *val, void *ctx)
{
  dev_scan_t *p_scan = (dev_scan_t *)ctx;

  if (p_scan->stop) return false;

  // 섹션이 바뀌면 앞의 항목이 완성된 것이다
  if (strcmp(sec, p_scan->cur_sec) != 0)
  {
    devFlush(p_scan);
    if (p_scan->stop) return false;

    snprintf(p_scan->cur_sec, sizeof(p_scan->cur_sec), "%s", sec);
    memset(&p_scan->cur, 0, sizeof(prog_dev_t));
    snprintf(p_scan->cur.name, sizeof(p_scan->cur.name), "%s", sec);
    p_scan->cur.id_mask = 0xFFFFFFFF;
    p_scan->cur.ap      = 0xFF;
  }

  if      (strcmp(key, "cpu")     == 0) snprintf(p_scan->cur.cpu,  sizeof(p_scan->cur.cpu),  "%s", val);
  else if (strcmp(key, "algo")    == 0) snprintf(p_scan->cur.algo, sizeof(p_scan->cur.algo), "%s", val);
  else if (strcmp(key, "id_addr") == 0) p_scan->cur.id_addr = cfgNum(val);
  else if (strcmp(key, "id_mask") == 0) p_scan->cur.id_mask = cfgNum(val);
  else if (strcmp(key, "id_val")  == 0) p_scan->cur.id_val  = cfgNum(val);
  else if (strcmp(key, "ram")     == 0) p_scan->cur.ram     = cfgNum(val);
  else if (strcmp(key, "ram_sz")  == 0) p_scan->cur.ram_sz  = cfgNum(val);
  else if (strcmp(key, "flash")   == 0) p_scan->cur.flash   = cfgNum(val);
  else if (strcmp(key, "flash_sz")== 0) p_scan->cur.flash_sz= cfgNum(val);
  else if (strcmp(key, "ap")      == 0) p_scan->cur.ap      = (uint8_t)cfgNum(val);

  return true;
}

static void devFlush(dev_scan_t *p_scan)
{
  if (p_scan->cur_sec[0] == 0) return;

  p_scan->cur.is_valid = true;
  if (p_scan->cb != NULL) p_scan->cb(&p_scan->cur, p_scan->ctx);

  p_scan->cur_sec[0] = 0;
}

static void devFindCb(const prog_dev_t *p_dev, void *ctx)
{
  dev_find_t *p_f = (dev_find_t *)ctx;

  if (p_f->found) return;
  if (devNameEq(p_dev->name, p_f->name) == false) return;

  *p_f->p_out = *p_dev;
  p_f->found  = true;
}

// 서로 다른 id_addr 만 모은다. 같은 주소를 쓰는 칩이 수십 개라 중복을 뺀다.
static void devAddrCb(const prog_dev_t *p_dev, void *ctx)
{
  dev_addr_t *p_a = (dev_addr_t *)ctx;

  if (p_dev->id_addr == 0) return;      // id_addr 이 없는 항목은 자동 판별 대상이 아니다

  for (uint32_t i = 0; i < p_a->cnt; i++)
  {
    if (p_a->addr[i] == p_dev->id_addr) return;
  }
  if (p_a->cnt < DEV_ADDR_MAX) p_a->addr[p_a->cnt++] = p_dev->id_addr;
}

static void devMatchCb(const prog_dev_t *p_dev, void *ctx)
{
  dev_match_t *p_m = (dev_match_t *)ctx;

  if (p_dev->id_addr == 0) return;

  for (uint32_t i = 0; i < p_m->p_addr->cnt; i++)
  {
    if (p_m->p_addr->addr[i] != p_dev->id_addr) continue;
    if (p_m->p_addr->ok[i] == false)            continue;

    if ((p_m->p_addr->val[i] & p_dev->id_mask) == p_dev->id_val)
    {
      if (p_m->match == 0) *p_m->p_out = *p_dev;
      p_m->match++;
    }
    return;
  }
}

static void devListCb(const prog_dev_t *p_dev, void *ctx)
{
  dev_list_t *p_l = (dev_list_t *)ctx;

  if (p_l->stop) return;

  p_l->cnt++;
  if (p_l->cb != NULL && p_l->cb(p_dev, p_l->ctx) == false) p_l->stop = true;
}

/* 이름 비교. 대소문자를 구분하지 않고 앞부분만 맞아도 된다 —
   DB 이름이 "STM32F411xC/E" 인데 사람은 "STM32F411" 이라고 친다. */
static bool devNameEq(const char *a, const char *b)
{
  uint32_t n = (uint32_t)strlen(b);

  if (n == 0) return false;
  return strncasecmp(a, b, n) == 0;
}


#endif
