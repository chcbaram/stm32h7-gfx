/*
 * prog_algo.c
 *
 *  플래시 알고리즘 공통 계층
 *
 *  포맷도 벤더도 모른다. 지울 섹터를 훑고, 조각을 타깃 버퍼로 옮기고, 되읽어
 *  비교하는 것까지가 여기 일이다. 실제 호출 규약은 algo_ops_t 뒤에 있다.
 *
 *  파일(.bin/.elf/.hex)과의 접점도 콜백 하나뿐이다 — "이 플래시 주소의 조각
 *  하나를 채워라". 그래서 포맷이 늘어도 굽기 루프는 그대로다.
 */

#include "prog/algo/prog_algo.h"
#include "prog/algo/prog_flm.h"
#include "prog/algo/prog_stldr.h"
#include "swd/swd_dap.h"


#ifdef _USE_HW_SWD


// 포맷 구현 목록. 여는 순서대로 probe 를 물어본다.
static const algo_ops_t *algo_ops_tbl[] =
{
  &flm_ops,
  &stldr_ops,
};


static swd_err_t algoEraseRange(algo_t *p_algo, uint32_t addr, uint32_t len,
                                algo_progress_t cb, void *ctx);

static bool (*algo_is_abort)(void);


void algoSetAbortCb(bool (*is_abort)(void))
{
  algo_is_abort = is_abort;
}

// 조각 경계에서만 묻는다. 알고리즘이 도는 중에 끊으면 타깃이 어중간해진다.
static bool algoAborted(void)
{
  return (algo_is_abort != NULL) && algo_is_abort();
}


// ----------------------------------------------------------------- 열기/닫기

bool algoOpen(algo_t *p_algo, const char *path)
{
  if (p_algo == NULL || path == NULL) return false;

  memset(p_algo, 0, sizeof(algo_t));
  p_algo->psize = ALGO_PSIZE_8;     // 타깃 전압을 모르는 동안은 가장 안전한 값

  if (elfOpen(&p_algo->elf, path) == false) return false;
  p_algo->is_open = true;

  if (elfIsArm32(&p_algo->elf) == false)
  {
    algoClose(p_algo);
    return false;
  }

  /* 확장자로 따지지 않는다. .FLM 은 FlashDevice, .stldr 은 StorageInfo 를
     들고 있으므로 그걸로 본다. 확장자는 사람이 붙여서 틀린다. */
  for (uint32_t i = 0; i < sizeof(algo_ops_tbl) / sizeof(algo_ops_tbl[0]); i++)
  {
    if (algo_ops_tbl[i]->probe(&p_algo->elf) == false) continue;

    p_algo->ops = algo_ops_tbl[i];
    if (p_algo->ops->parse(p_algo) == true) return true;

    break;
  }

  algoClose(p_algo);
  return false;
}

void algoClose(algo_t *p_algo)
{
  if (p_algo != NULL && p_algo->is_open)
  {
    elfClose(&p_algo->elf);
    p_algo->is_open   = false;
    p_algo->is_loaded = false;
  }
}

const char *algoKindStr(algo_t *p_algo)
{
  if (p_algo == NULL || p_algo->ops == NULL) return "?";
  return p_algo->ops->name;
}


// ----------------------------------------------------------------- 공개

swd_err_t algoLoad(algo_t *p_algo, uint32_t ram_base, uint32_t ram_size)
{
  if (p_algo == NULL || p_algo->is_open == false) return SWD_ERR_PROTOCOL;

  return p_algo->ops->load(p_algo, ram_base, ram_size);
}

void algoSetPSize(algo_t *p_algo, uint32_t psize)
{
  if (p_algo != NULL && psize <= ALGO_PSIZE_64) p_algo->psize = psize;
}

// ----------------------------------------------------------------- 섹터

bool algoIsInRange(algo_t *p_algo, uint32_t addr)
{
  if (p_algo == NULL || p_algo->is_open == false) return false;

  return (addr >= p_algo->dev.dev_adr) &&
         (addr <  p_algo->dev.dev_adr + p_algo->dev.sz_dev);
}

uint32_t algoSectorSize(algo_t *p_algo, uint32_t addr)
{
  uint32_t off;
  uint32_t size = 0;

  if (algoIsInRange(p_algo, addr) == false)          return 0;
  if (p_algo->dev.sector_cnt == 0)                  return 0;

  off = addr - p_algo->dev.dev_adr;

  for (uint32_t i = 0; i < p_algo->dev.sector_cnt; i++)
  {
    if (off >= p_algo->dev.sector[i].addr)
    {
      size = p_algo->dev.sector[i].size;
    }
    else
    {
      break;
    }
  }
  return size;
}

uint32_t algoSectorBase(algo_t *p_algo, uint32_t addr)
{
  uint32_t off;
  uint32_t grp_off = 0;
  uint32_t size    = 0;

  // 범위 밖이면 0 을 돌려준다. 예전엔 주소를 그대로 돌려줬는데, 잘린 인자가
  // 그대로 통과해 엉뚱한 섹터를 지우는 사고가 났다.
  if (algoIsInRange(p_algo, addr) == false)          return 0;
  if (p_algo->dev.sector_cnt == 0)                  return 0;

  off = addr - p_algo->dev.dev_adr;

  for (uint32_t i = 0; i < p_algo->dev.sector_cnt; i++)
  {
    if (off >= p_algo->dev.sector[i].addr)
    {
      grp_off = p_algo->dev.sector[i].addr;
      size    = p_algo->dev.sector[i].size;
    }
    else
    {
      break;
    }
  }
  if (size == 0) return addr;

  // 같은 크기의 섹터가 grp_off 부터 반복된다
  return p_algo->dev.dev_adr + grp_off + ((off - grp_off) / size) * size;
}


// ----------------------------------------------------------------- 파일 굽기

/* 파일이 차지하는 범위의 섹터를 전부 지운다. 같은 섹터를 두 번 지우지 않게
   섹터 시작 주소로 건너뛴다. */
static swd_err_t algoEraseRange(algo_t *p_algo, uint32_t addr, uint32_t len,
                               algo_progress_t cb, void *ctx)
{
  uint32_t  cur = algoSectorBase(p_algo, addr);
  uint32_t  end = addr + len;
  swd_err_t err;

  if (cur == 0) return SWD_ERR_PROTOCOL;

  err = p_algo->ops->init(p_algo, ALGO_FNC_ERASE);
  if (err != SWD_OK) return err;

  while (cur < end)
  {
    uint32_t size = algoSectorSize(p_algo, cur);

    if (algoAborted())
    {
      p_algo->ops->uninit(p_algo, ALGO_FNC_ERASE);
      return SWD_ERR_ABORT;
    }

    if (size == 0)
    {
      p_algo->ops->uninit(p_algo, ALGO_FNC_ERASE);
      return SWD_ERR_PROTOCOL;
    }
    if (cb) cb("erase", cur, cur - algoSectorBase(p_algo, addr), end - addr, ctx);

    err = p_algo->ops->erase_sector(p_algo, cur, size);
    if (err != SWD_OK)
    {
      p_algo->ops->uninit(p_algo, ALGO_FNC_ERASE);
      return err;
    }
    cur += size;
  }

  return p_algo->ops->uninit(p_algo, ALGO_FNC_ERASE);
}

/* 페이지 한 장을 채우는 콜백. 소스(.bin / .elf)와 굽기 파이프라인을 떼어놓는
   유일한 접점이다. addr 은 플래시 주소이고, 소스에 그 범위의 데이터가 없으면
   val_empty 로 채워서라도 len 바이트를 전부 채워야 한다. */
typedef bool (*algo_fill_t)(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len);

typedef struct
{
  FIL     *file;
  uint32_t base;        // 파일 오프셋 0 이 놓일 플래시 주소
  uint32_t size;
  uint8_t  empty;
} algo_bin_src_t;

typedef struct
{
  hex_t   *hex;
  uint8_t  empty;
} algo_hex_src_t;

typedef struct
{
  elf_t      *elf;
  elf_phdr_t  seg[ALGO_ELF_SEG_MAX];
  uint32_t    seg_cnt;
  uint8_t     empty;
} algo_elf_src_t;


static bool algoFillBin(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len)
{
  algo_bin_src_t *p_src = (algo_bin_src_t *)src;
  uint32_t       off   = addr - p_src->base;
  uint32_t       n     = (off < p_src->size) ? (p_src->size - off) : 0;
  UINT           br    = 0;

  if (n > len) n = len;
  if (n < len) memset(&p_buf[n], p_src->empty, len - n);
  if (n == 0)  return true;

  /* 순차 호출이라 대개 이미 그 위치다. FatFs 의 f_lseek 는 뒤로 갈 때 클러스터
     체인을 처음부터 다시 걷기 때문에 같은 위치인데도 부르면 손해다. */
  if (f_tell(p_src->file) != off && f_lseek(p_src->file, off) != FR_OK) return false;

  return (f_read(p_src->file, p_buf, n, &br) == FR_OK) && (br == n);
}

/* 세그먼트 사이의 빈틈은 val_empty 로 남는다. 어차피 그 구간도 지웠으므로
   되읽으면 그대로 일치한다. */
static bool algoFillElf(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len)
{
  algo_elf_src_t *p_src = (algo_elf_src_t *)src;

  memset(p_buf, p_src->empty, len);

  for (uint32_t i = 0; i < p_src->seg_cnt; i++)
  {
    elf_phdr_t *p_seg = &p_src->seg[i];
    uint32_t    lo    = (p_seg->paddr > addr) ? p_seg->paddr : addr;
    uint32_t    hi    = p_seg->paddr + p_seg->filesz;

    if (hi > addr + len) hi = addr + len;
    if (lo >= hi)        continue;

    if (elfRead(p_src->elf, p_seg->offset + (lo - p_seg->paddr),
                &p_buf[lo - addr], hi - lo) == false)
    {
      return false;
    }
  }
  return true;
}

/* 이중 버퍼 파이프라인.
     1. 페이지 N 을 타깃 버퍼에 올린다
     2. ProgramPage 를 시작만 하고 기다리지 않는다
     3. 타깃이 굽는 동안 페이지 N+1 을 읽어 다른 버퍼에 올린다
     4. 그제서야 N 의 완료를 기다린다
   AHB-AP 는 코어와 독립적으로 동작하므로 3번이 가능하다.
   Init(PROGRAM) 은 호출부가 이미 해둔 상태여야 한다. */
static swd_err_t algoProgramRange(algo_t *p_algo, uint32_t addr, uint32_t len,
                                 algo_fill_t fill, void *src,
                                 algo_progress_t cb, void *ctx, algo_time_t *p_tm)
{
  static uint8_t page[ALGO_BUF_MAX] __attribute__((aligned(32)));
  uint32_t  buf[2]    = { p_algo->buf_addr, p_algo->buf_addr_b };
  uint32_t  sz_page   = p_algo->buf_size;
  uint32_t  idx       = 0;
  uint32_t  done      = 0;
  uint32_t  armed_len = 0;
  bool      armed     = false;
  swd_err_t err       = SWD_OK;
  uint32_t  t0;

  while (done < len || armed)
  {
    uint32_t n = 0;

    if (algoAborted() && armed == false)
    {
      err = SWD_ERR_ABORT;
      break;
    }

    // 다음 페이지를 준비한다 (타깃은 이전 페이지를 굽는 중일 수 있다)
    if (done + armed_len < len)
    {
      n = len - (done + armed_len);
      if (n > sz_page) n = sz_page;

      t0 = millis();
      if (fill(src, addr + done + armed_len, page, sz_page) == false)
      {
        err = SWD_ERR_PROTOCOL;
        break;
      }
      p_tm->read_ms += millis() - t0;

      t0 = millis();
      if (algoWriteMem(buf[idx], page, sz_page, p_algo) == false)
      {
        err = SWD_ERR_FAULT;
        break;
      }
      p_tm->xfer_ms += millis() - t0;
    }

    // 앞서 시작해 둔 것이 있으면 이제 거둔다
    if (armed)
    {
      t0  = millis();
      err = p_algo->ops->prog_wait(p_algo, p_algo->dev.to_prog);
      p_tm->call_ms += millis() - t0;
      armed = false;

      if (err != SWD_OK) break;
      done     += armed_len;
      armed_len = 0;
      p_tm->page_cnt++;
      if (cb) cb("program", addr + done, done, len, ctx);
    }

    // 준비된 페이지가 있으면 굽기를 시작만 한다
    if (n > 0)
    {
      t0  = millis();
      err = p_algo->ops->prog_start(p_algo, addr + done, sz_page, buf[idx]);
      p_tm->call_ms += millis() - t0;
      if (err != SWD_OK) break;

      armed     = true;
      armed_len = n;
      idx      ^= 1;
    }
  }

  return err;
}

/* 되읽어 소스와 비교한다. 굽기와 같은 fill 콜백을 쓰므로 세그먼트 사이 빈틈의
   패딩까지 그대로 검증되고, SD 읽기가 깨진 경우도 여기서 걸린다. */
static swd_err_t algoVerifyRange(algo_t *p_algo, uint32_t addr, uint32_t len,
                                algo_fill_t fill, void *src,
                                algo_progress_t cb, void *ctx, uint32_t *p_bad)
{
  static uint8_t  page[ALGO_BUF_MAX] __attribute__((aligned(32)));
  static uint32_t rd[ALGO_BUF_MAX / 4];
  uint32_t        done = 0;
  uint32_t        bad  = 0;
  swd_err_t       err  = SWD_OK;

  while (done < len)
  {
    uint32_t n = p_algo->buf_size;

    if (algoAborted()) { err = SWD_ERR_ABORT; break; }

    if (n > len - done) n = len - done;

    if (fill(src, addr + done, page, n) == false) { err = SWD_ERR_PROTOCOL; break; }

    /* 외부 메모리는 직접 못 읽는다. 알고리즘이 Read 를 갖고 있으면 타깃 RAM
       버퍼로 옮기게 하고 그 버퍼를 읽는다. */
    if (p_algo->ops->read != NULL && p_algo->dev.dev_type == ALGO_DEV_EXTERNAL)
    {
      err = p_algo->ops->read(p_algo, addr + done, n, p_algo->buf_addr);
      if (err != SWD_OK) break;
      err = swdMemReadBlock(p_algo->buf_addr, rd, (n + 3) / 4);
    }
    else
    {
      err = swdMemReadBlock(addr + done, rd, (n + 3) / 4);
    }
    if (err != SWD_OK) break;

    for (uint32_t i = 0; i < n; i++)
    {
      if (((uint8_t *)rd)[i] != page[i]) bad++;
    }
    done += n;
    if (cb) cb("verify", addr + done, done, len, ctx);
  }

  if (p_bad) *p_bad = bad;
  return err;
}

swd_err_t algoWriteFile(algo_t *p_algo, const char *path, uint32_t addr,
                       algo_progress_t cb, void *ctx, uint32_t *p_written,
                       algo_time_t *p_time)
{
  algo_bin_src_t src;
  algo_time_t    tm;
  FIL           file;
  swd_err_t     err;
  uint32_t      size;
  uint32_t      t0;

  memset(&tm, 0, sizeof(tm));
  if (p_written) *p_written = 0;
  if (p_time)    *p_time    = tm;

  if (p_algo == NULL || p_algo->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (algoIsInRange(p_algo, addr) == false)         return SWD_ERR_PROTOCOL;
  if (p_algo->buf_size == 0)                       return SWD_ERR_PROTOCOL;

  if (f_open(&file, path, FA_READ) != FR_OK) return SWD_ERR_PROTOCOL;
  size = (uint32_t)f_size(&file);

  if (size == 0 || algoIsInRange(p_algo, addr + size - 1) == false)
  {
    f_close(&file);
    return SWD_ERR_PROTOCOL;
  }

  src.file  = &file;
  src.base  = addr;
  src.size  = size;
  src.empty = p_algo->dev.val_empty;

  t0  = millis();
  err = algoEraseRange(p_algo, addr, size, cb, ctx);
  tm.erase_ms = millis() - t0;

  if (err == SWD_OK)
  {
    err = p_algo->ops->init(p_algo, ALGO_FNC_PROGRAM);
    if (err == SWD_OK)
    {
      err = algoProgramRange(p_algo, addr, size, algoFillBin, &src, cb, ctx, &tm);
      p_algo->ops->uninit(p_algo, ALGO_FNC_PROGRAM);
    }
  }

  f_close(&file);

  if (p_written) *p_written = (uint32_t)tm.page_cnt * p_algo->buf_size;
  if (p_written && *p_written > size) *p_written = size;
  if (p_time)    *p_time    = tm;
  return err;
}

swd_err_t algoVerifyFile(algo_t *p_algo, const char *path, uint32_t addr,
                        algo_progress_t cb, void *ctx, uint32_t *p_bad)
{
  algo_bin_src_t src;
  FIL           file;
  swd_err_t     err;

  if (p_algo == NULL) return SWD_ERR_PROTOCOL;
  if (f_open(&file, path, FA_READ) != FR_OK) return SWD_ERR_PROTOCOL;

  src.file  = &file;
  src.base  = addr;
  src.size  = (uint32_t)f_size(&file);
  src.empty = p_algo->dev.val_empty;

  err = algoVerifyRange(p_algo, addr, src.size, algoFillBin, &src, cb, ctx, p_bad);

  f_close(&file);
  return err;
}


// ----------------------------------------------------------------- ELF 굽기

/* .elf 는 굽는 주소가 파일 안에 있다. PT_LOAD 세그먼트의 p_paddr 이 그것이고,
   .data 처럼 vaddr(RAM)과 paddr(플래시)이 다른 세그먼트가 있으므로 반드시
   paddr 을 봐야 한다. 그래서 .bin 과 달리 주소 인자가 필요 없다. */
static bool algoElfCollect(elf_t *p_elf, algo_elf_src_t *p_src)
{
  elf_phdr_t ph;

  p_src->elf     = p_elf;
  p_src->seg_cnt = 0;

  for (uint32_t i = 0; i < p_elf->e_phnum; i++)
  {
    if (elfGetPhdr(p_elf, i, &ph) == false) continue;
    if (ph.type != ELF_PT_LOAD)             continue;
    if (ph.filesz == 0)                     continue;

    if (p_src->seg_cnt >= ALGO_ELF_SEG_MAX) return false;
    p_src->seg[p_src->seg_cnt++] = ph;
  }
  return (p_src->seg_cnt > 0);
}

/* 굽기 범위를 정한다. 시작은 ProgramPage 를 위해 페이지 경계로 내리고,
   앞쪽에 생긴 여백은 fill 이 val_empty 로 채운다. */
static bool algoElfRange(algo_t *p_algo, elf_t *p_elf, uint32_t *p_addr, uint32_t *p_len)
{
  uint32_t lo, hi;

  if (elfGetLoadRange(p_elf, &lo, &hi) == false) return false;
  if (algoIsInRange(p_algo, lo) == false)          return false;
  if (algoIsInRange(p_algo, hi - 1) == false)      return false;

  lo -= (lo - p_algo->dev.dev_adr) % p_algo->buf_size;

  *p_addr = lo;
  *p_len  = hi - lo;
  return true;
}

swd_err_t algoWriteElf(algo_t *p_algo, const char *path,
                      algo_progress_t cb, void *ctx, uint32_t *p_written,
                      algo_time_t *p_time, uint32_t *p_addr)
{
  algo_elf_src_t src;
  algo_time_t    tm;
  elf_t         elf;
  swd_err_t     err;
  uint32_t      addr, len, t0;

  memset(&tm, 0, sizeof(tm));
  if (p_written) *p_written = 0;
  if (p_time)    *p_time    = tm;

  if (p_algo == NULL || p_algo->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_algo->buf_size == 0)                       return SWD_ERR_PROTOCOL;

  if (elfOpen(&elf, path) == false) return SWD_ERR_PROTOCOL;

  src.empty = p_algo->dev.val_empty;
  if (algoElfCollect(&elf, &src) == false ||
      algoElfRange(p_algo, &elf, &addr, &len) == false)
  {
    elfClose(&elf);
    return SWD_ERR_PROTOCOL;
  }
  if (p_addr) *p_addr = addr;

  t0  = millis();
  err = algoEraseRange(p_algo, addr, len, cb, ctx);
  tm.erase_ms = millis() - t0;

  if (err == SWD_OK)
  {
    err = p_algo->ops->init(p_algo, ALGO_FNC_PROGRAM);
    if (err == SWD_OK)
    {
      err = algoProgramRange(p_algo, addr, len, algoFillElf, &src, cb, ctx, &tm);
      p_algo->ops->uninit(p_algo, ALGO_FNC_PROGRAM);
    }
  }

  elfClose(&elf);

  if (p_written) *p_written = (err == SWD_OK) ? len : 0;
  if (p_time)    *p_time    = tm;
  return err;
}

static bool algoFillHex(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len)
{
  algo_hex_src_t *p_src = (algo_hex_src_t *)src;

  return hexFill(p_src->hex, addr, p_buf, len, p_src->empty);
}

/* .hex 도 주소가 파일 안에 있다. 다만 .elf 처럼 세그먼트로 묶여 있지 않고
   레코드마다 흩어져 있어서, 굽는 범위는 열 때의 전체 스캔으로 얻는다. */
static bool algoHexRange(algo_t *p_algo, hex_t *p_hex, uint32_t *p_addr, uint32_t *p_len)
{
  uint32_t lo = p_hex->lo;

  if (algoIsInRange(p_algo, lo) == false)             return false;
  if (algoIsInRange(p_algo, p_hex->hi - 1) == false)  return false;

  lo -= (lo - p_algo->dev.dev_adr) % p_algo->buf_size;

  *p_addr = lo;
  *p_len  = p_hex->hi - lo;
  return true;
}

swd_err_t algoWriteHex(algo_t *p_algo, const char *path,
                      algo_progress_t cb, void *ctx, uint32_t *p_written,
                      algo_time_t *p_time, uint32_t *p_addr)
{
  static hex_t  hex;
  algo_hex_src_t src;
  algo_time_t    tm;
  swd_err_t     err;
  uint32_t      addr, len, t0;

  memset(&tm, 0, sizeof(tm));
  if (p_written) *p_written = 0;
  if (p_time)    *p_time    = tm;

  if (p_algo == NULL || p_algo->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_algo->buf_size == 0)                       return SWD_ERR_PROTOCOL;

  if (hexOpen(&hex, path) == false) return SWD_ERR_PROTOCOL;

  src.hex   = &hex;
  src.empty = p_algo->dev.val_empty;

  if (algoHexRange(p_algo, &hex, &addr, &len) == false)
  {
    hexClose(&hex);
    return SWD_ERR_PROTOCOL;
  }
  if (p_addr) *p_addr = addr;

  t0  = millis();
  err = algoEraseRange(p_algo, addr, len, cb, ctx);
  tm.erase_ms = millis() - t0;

  if (err == SWD_OK)
  {
    err = p_algo->ops->init(p_algo, ALGO_FNC_PROGRAM);
    if (err == SWD_OK)
    {
      err = algoProgramRange(p_algo, addr, len, algoFillHex, &src, cb, ctx, &tm);
      p_algo->ops->uninit(p_algo, ALGO_FNC_PROGRAM);
    }
  }

  hexClose(&hex);

  if (p_written) *p_written = (err == SWD_OK) ? len : 0;
  if (p_time)    *p_time    = tm;
  return err;
}

swd_err_t algoVerifyHex(algo_t *p_algo, const char *path,
                       algo_progress_t cb, void *ctx, uint32_t *p_bad)
{
  static hex_t  hex;
  algo_hex_src_t src;
  swd_err_t     err;
  uint32_t      addr, len;

  if (p_algo == NULL) return SWD_ERR_PROTOCOL;
  if (hexOpen(&hex, path) == false) return SWD_ERR_PROTOCOL;

  src.hex   = &hex;
  src.empty = p_algo->dev.val_empty;

  if (algoHexRange(p_algo, &hex, &addr, &len) == false)
  {
    hexClose(&hex);
    return SWD_ERR_PROTOCOL;
  }

  err = algoVerifyRange(p_algo, addr, len, algoFillHex, &src, cb, ctx, p_bad);

  hexClose(&hex);
  return err;
}

swd_err_t algoVerifyElf(algo_t *p_algo, const char *path,
                       algo_progress_t cb, void *ctx, uint32_t *p_bad)
{
  algo_elf_src_t src;
  elf_t         elf;
  swd_err_t     err;
  uint32_t      addr, len;

  if (p_algo == NULL) return SWD_ERR_PROTOCOL;
  if (elfOpen(&elf, path) == false) return SWD_ERR_PROTOCOL;

  src.empty = p_algo->dev.val_empty;
  if (algoElfCollect(&elf, &src) == false ||
      algoElfRange(p_algo, &elf, &addr, &len) == false)
  {
    elfClose(&elf);
    return SWD_ERR_PROTOCOL;
  }

  err = algoVerifyRange(p_algo, addr, len, algoFillElf, &src, cb, ctx, p_bad);

  elfClose(&elf);
  return err;
}

// ----------------------------------------------------------------- 내부

/* 타깃 RAM 으로 흘려보낸다. 섹션 크기도 주소도 4의 배수라는 보장이 없어서
   앞뒤 자투리는 8비트로, 가운데만 블록 전송한다. */
bool algoWriteMem(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx)
{
  static uint32_t word_buf[64] __attribute__((aligned(4)));

  (void)ctx;

  if (p_data == NULL)
  {
    while (len > 0 && (addr & 3))
    {
      if (swdMemWrite8(addr, 0) != SWD_OK) return false;
      addr++; len--;
    }
    if (len >= 4)
    {
      if (swdMemFill(addr, 0, len / 4) != SWD_OK) return false;
      addr += (len & ~3UL); len &= 3;
    }
    while (len > 0)
    {
      if (swdMemWrite8(addr, 0) != SWD_OK) return false;
      addr++; len--;
    }
    return true;
  }

  while (len > 0 && (addr & 3))
  {
    if (swdMemWrite8(addr, *p_data) != SWD_OK) return false;
    addr++; p_data++; len--;
  }
  while (len >= 4)
  {
    uint32_t n = len & ~3UL;

    if (n > sizeof(word_buf)) n = sizeof(word_buf);
    memcpy(word_buf, p_data, n);
    if (swdMemWriteBlock(addr, word_buf, n / 4) != SWD_OK) return false;
    addr += n; p_data += n; len -= n;
  }
  while (len > 0)
  {
    if (swdMemWrite8(addr, *p_data) != SWD_OK) return false;
    addr++; p_data++; len--;
  }
  return true;
}

/* .FLM 은 성공 시 0 을 반환한다. 이 극성을 여기서 흡수해서 호출부에
   흩어놓지 않는다. .stldr 은 1 이 성공이라 나중에 여기만 바꾸면 된다. */


#endif
