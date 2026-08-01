/*
 * prog_flm.c
 *
 *  CMSIS-Pack 플래시 알고리즘(.FLM) 바인딩
 *
 *  FlashDevice 구조체 배치 (ARM FlashOS.h, 섹터 배열 전까지 160바이트)
 *      off  size
 *        0    2   Vers
 *        2  128   DevName
 *      130    2   DevType
 *      132    4   DevAdr
 *      136    4   szDev
 *      140    4   szPage
 *      144    4   Res
 *      148    1   valEmpty  (+3 패딩)
 *      152    4   toProg    (ms)
 *      156    4   toErase   (ms)
 *      160  8×N   { szSector, AddrSector }  0xFFFFFFFF 쌍으로 끝
 *
 *  AddrSector 는 절대 주소가 아니라 DevAdr 기준 상대 오프셋이다.
 *  그리고 그 섹터 크기는 다음 항목이 나올 때까지 유지된다.
 */

#include "prog/prog_flm.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"


#ifdef _USE_HW_SWD


#define FLM_DEVDSCR_NAME    "DevDscr"
#define FLM_STACK_SIZE      0x800
#define FLM_CODE_OFFS       SWD_ALGO_CODE_OFFS
#define FLM_INIT_TIMEOUT    2000
#define FLM_CHIP_TIMEOUT    60000
#define FLM_CLK_DEF         8000000
#define FLM_PAGE_MAX        4096

static uint32_t flm_xfer_ms;      // flmProgramPage 안의 전송 시간 누적


static uint32_t flmU16(const uint8_t *p);
static uint32_t flmU32(const uint8_t *p);
static bool     flmReadDevice(flm_t *p_flm);
static bool     flmLoadCb(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx);
static swd_err_t flmCall(flm_t *p_flm, uint32_t pc,
                         uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                         uint32_t timeout_ms);


// ----------------------------------------------------------------- 열기/닫기

bool flmOpen(flm_t *p_flm, const char *path)
{
  if (p_flm == NULL || path == NULL) return false;

  memset(p_flm, 0, sizeof(flm_t));

  if (elfOpen(&p_flm->elf, path) == false)
  {
    return false;
  }
  if (elfIsArm32(&p_flm->elf) == false)
  {
    elfClose(&p_flm->elf);
    return false;
  }

  // 심볼은 Thumb 비트가 붙은 채로 나온다. 재배치할 때 그대로 더한다.
  if (elfFindSym(&p_flm->elf, "Init",        &p_flm->fn_init)   == false ||
      elfFindSym(&p_flm->elf, "UnInit",      &p_flm->fn_uninit) == false ||
      elfFindSym(&p_flm->elf, "ProgramPage", &p_flm->fn_program) == false)
  {
    elfClose(&p_flm->elf);
    return false;
  }
  // 이 둘은 없을 수도 있다
  elfFindSym(&p_flm->elf, "EraseSector", &p_flm->fn_erase_sector);
  elfFindSym(&p_flm->elf, "EraseChip",   &p_flm->fn_erase_chip);

  if (flmReadDevice(p_flm) == false)
  {
    elfClose(&p_flm->elf);
    return false;
  }

  p_flm->is_open = true;
  return true;
}

void flmClose(flm_t *p_flm)
{
  if (p_flm != NULL && p_flm->is_open)
  {
    elfClose(&p_flm->elf);
    p_flm->is_open   = false;
    p_flm->is_loaded = false;
  }
}


// ----------------------------------------------------------------- 로드

swd_err_t flmLoad(flm_t *p_flm, uint32_t ram_base, uint32_t ram_size)
{
  swd_err_t err;
  uint32_t  base = 0;
  uint32_t  lo = 0, hi = 0;
  uint32_t  code_addr;

  if (p_flm == NULL || p_flm->is_open == false) return SWD_ERR_PROTOCOL;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  ram_base = (ram_base + 7) & ~7UL;
  p_flm->ram_base = ram_base;
  p_flm->ram_end  = ram_base + ram_size;

  /* 아레나 앞머리는 알고리즘 러너가 쓰는 BKPT 트램폴린이다.
     코드는 그 뒤부터 올린다. */
  code_addr = ram_base + FLM_CODE_OFFS;

  if (elfGetAllocBase(&p_flm->elf, FLM_DEVDSCR_NAME, &base) == false)
  {
    return SWD_ERR_PROTOCOL;
  }
  p_flm->delta = (int32_t)code_addr - (int32_t)base;

  err = swdMemWrite32(ram_base + SWD_ALGO_BKPT_OFFS, SWD_ALGO_BKPT_WORD);
  if (err != SWD_OK) return err;

  if (elfLoadSections(&p_flm->elf, p_flm->delta, FLM_DEVDSCR_NAME,
                      flmLoadCb, p_flm, &lo, &hi) == false)
  {
    return SWD_ERR_FAULT;
  }

  // 스택은 로드된 영역 뒤에, 버퍼는 다시 그 뒤에 둔다
  p_flm->algo.code_addr   = code_addr;
  p_flm->algo.bkpt_addr   = ram_base + SWD_ALGO_BKPT_OFFS;
  p_flm->algo.static_base = 0;
  p_flm->algo.stack_top   = (hi + FLM_STACK_SIZE + 7) & ~7UL;

  /* 버퍼를 두 개 잡는다. 타깃이 A 를 굽는 동안 B 를 채워 넣기 위해서다. */
  p_flm->buf_addr    = (p_flm->algo.stack_top + 3) & ~3UL;
  p_flm->buf_addr_b  = p_flm->buf_addr + ((p_flm->dev.sz_page + 3) & ~3UL);

  /* PrgData 가 있으면 그 주소가 R9(static base)다. RWPI 로 빌드된 알고리즘은
     전역 접근에 R9 를 쓴다. */
  {
    elf_sec_t sec;

    if (elfFindSec(&p_flm->elf, "PrgData", &sec) == true && sec.size > 0)
    {
      p_flm->algo.static_base = (uint32_t)((int32_t)sec.addr + p_flm->delta);
    }
  }

  if (p_flm->buf_addr_b + p_flm->dev.sz_page > p_flm->ram_end)
  {
    return SWD_ERR_PROTOCOL;      // 아레나가 RAM 을 넘는다
  }

  p_flm->is_loaded = true;
  return SWD_OK;
}


// ----------------------------------------------------------------- 알고리즘 호출

swd_err_t flmInit(flm_t *p_flm, uint32_t addr, uint32_t clk, uint32_t fnc)
{
  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;

  return flmCall(p_flm, p_flm->fn_init + (uint32_t)p_flm->delta,
                 addr, clk, fnc, 0, FLM_INIT_TIMEOUT);
}

swd_err_t flmUnInit(flm_t *p_flm, uint32_t fnc)
{
  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;

  return flmCall(p_flm, p_flm->fn_uninit + (uint32_t)p_flm->delta,
                 fnc, 0, 0, 0, FLM_INIT_TIMEOUT);
}

swd_err_t flmEraseSector(flm_t *p_flm, uint32_t addr)
{
  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_flm->fn_erase_sector == 0)                return SWD_ERR_PROTOCOL;
  if (flmIsInRange(p_flm, addr) == false)         return SWD_ERR_PROTOCOL;

  return flmCall(p_flm, p_flm->fn_erase_sector + (uint32_t)p_flm->delta,
                 addr, 0, 0, 0, p_flm->dev.to_erase);
}

swd_err_t flmEraseChip(flm_t *p_flm)
{
  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_flm->fn_erase_chip == 0)                  return SWD_ERR_PROTOCOL;

  return flmCall(p_flm, p_flm->fn_erase_chip + (uint32_t)p_flm->delta,
                 0, 0, 0, 0, FLM_CHIP_TIMEOUT);
}

swd_err_t flmProgramPage(flm_t *p_flm, uint32_t addr, const uint8_t *p_data, uint32_t len)
{
  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_data == NULL || len == 0)                 return SWD_ERR_PROTOCOL;
  if (len > p_flm->dev.sz_page)                   return SWD_ERR_PROTOCOL;
  if (flmIsInRange(p_flm, addr) == false)         return SWD_ERR_PROTOCOL;

  // 굽을 데이터를 타깃 버퍼에 먼저 올린다
  {
    uint32_t t0 = millis();

    if (flmLoadCb(p_flm->buf_addr, p_data, len, p_flm) == false)
    {
      return SWD_ERR_FAULT;
    }
    flm_xfer_ms += millis() - t0;
  }

  return flmCall(p_flm, p_flm->fn_program + (uint32_t)p_flm->delta,
                 addr, len, p_flm->buf_addr, 0, p_flm->dev.to_prog);
}


// ----------------------------------------------------------------- 섹터

bool flmIsInRange(flm_t *p_flm, uint32_t addr)
{
  if (p_flm == NULL || p_flm->is_open == false) return false;

  return (addr >= p_flm->dev.dev_adr) &&
         (addr <  p_flm->dev.dev_adr + p_flm->dev.sz_dev);
}

uint32_t flmSectorSize(flm_t *p_flm, uint32_t addr)
{
  uint32_t off;
  uint32_t size = 0;

  if (flmIsInRange(p_flm, addr) == false)          return 0;
  if (p_flm->dev.sector_cnt == 0)                  return 0;

  off = addr - p_flm->dev.dev_adr;

  for (uint32_t i = 0; i < p_flm->dev.sector_cnt; i++)
  {
    if (off >= p_flm->dev.sector[i].addr)
    {
      size = p_flm->dev.sector[i].size;
    }
    else
    {
      break;
    }
  }
  return size;
}

uint32_t flmSectorBase(flm_t *p_flm, uint32_t addr)
{
  uint32_t off;
  uint32_t grp_off = 0;
  uint32_t size    = 0;

  // 범위 밖이면 0 을 돌려준다. 예전엔 주소를 그대로 돌려줬는데, 잘린 인자가
  // 그대로 통과해 엉뚱한 섹터를 지우는 사고가 났다.
  if (flmIsInRange(p_flm, addr) == false)          return 0;
  if (p_flm->dev.sector_cnt == 0)                  return 0;

  off = addr - p_flm->dev.dev_adr;

  for (uint32_t i = 0; i < p_flm->dev.sector_cnt; i++)
  {
    if (off >= p_flm->dev.sector[i].addr)
    {
      grp_off = p_flm->dev.sector[i].addr;
      size    = p_flm->dev.sector[i].size;
    }
    else
    {
      break;
    }
  }
  if (size == 0) return addr;

  // 같은 크기의 섹터가 grp_off 부터 반복된다
  return p_flm->dev.dev_adr + grp_off + ((off - grp_off) / size) * size;
}


// ----------------------------------------------------------------- 파일 굽기

/* 파일이 차지하는 범위의 섹터를 전부 지운다. 같은 섹터를 두 번 지우지 않게
   섹터 시작 주소로 건너뛴다. */
static swd_err_t flmEraseRange(flm_t *p_flm, uint32_t addr, uint32_t len,
                               flm_progress_t cb, void *ctx)
{
  uint32_t  cur = flmSectorBase(p_flm, addr);
  uint32_t  end = addr + len;
  swd_err_t err;

  if (cur == 0) return SWD_ERR_PROTOCOL;

  err = flmInit(p_flm, p_flm->dev.dev_adr, FLM_CLK_DEF, FLM_FNC_ERASE);
  if (err != SWD_OK) return err;

  while (cur < end)
  {
    uint32_t size = flmSectorSize(p_flm, cur);

    if (size == 0)
    {
      flmUnInit(p_flm, FLM_FNC_ERASE);
      return SWD_ERR_PROTOCOL;
    }
    if (cb) cb("erase", cur, cur - flmSectorBase(p_flm, addr), end - addr, ctx);

    err = flmEraseSector(p_flm, cur);
    if (err != SWD_OK)
    {
      flmUnInit(p_flm, FLM_FNC_ERASE);
      return err;
    }
    cur += size;
  }

  return flmUnInit(p_flm, FLM_FNC_ERASE);
}

/* 페이지 한 장을 채우는 콜백. 소스(.bin / .elf)와 굽기 파이프라인을 떼어놓는
   유일한 접점이다. addr 은 플래시 주소이고, 소스에 그 범위의 데이터가 없으면
   val_empty 로 채워서라도 len 바이트를 전부 채워야 한다. */
typedef bool (*flm_fill_t)(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len);

typedef struct
{
  FIL     *file;
  uint32_t base;        // 파일 오프셋 0 이 놓일 플래시 주소
  uint32_t size;
  uint8_t  empty;
} flm_bin_src_t;

typedef struct
{
  hex_t   *hex;
  uint8_t  empty;
} flm_hex_src_t;

typedef struct
{
  elf_t      *elf;
  elf_phdr_t  seg[FLM_ELF_SEG_MAX];
  uint32_t    seg_cnt;
  uint8_t     empty;
} flm_elf_src_t;


static bool flmFillBin(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len)
{
  flm_bin_src_t *p_src = (flm_bin_src_t *)src;
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
static bool flmFillElf(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len)
{
  flm_elf_src_t *p_src = (flm_elf_src_t *)src;

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
static swd_err_t flmProgramRange(flm_t *p_flm, uint32_t addr, uint32_t len,
                                 flm_fill_t fill, void *src,
                                 flm_progress_t cb, void *ctx, flm_time_t *p_tm)
{
  static uint8_t page[FLM_PAGE_MAX] __attribute__((aligned(32)));
  uint32_t  buf[2]    = { p_flm->buf_addr, p_flm->buf_addr_b };
  uint32_t  sz_page   = p_flm->dev.sz_page;
  uint32_t  idx       = 0;
  uint32_t  done      = 0;
  uint32_t  armed_len = 0;
  bool      armed     = false;
  swd_err_t err       = SWD_OK;
  uint32_t  t0;

  while (done < len || armed)
  {
    uint32_t n = 0;

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
      if (flmLoadCb(buf[idx], page, sz_page, p_flm) == false)
      {
        err = SWD_ERR_FAULT;
        break;
      }
      p_tm->xfer_ms += millis() - t0;
    }

    // 앞서 시작해 둔 것이 있으면 이제 거둔다
    if (armed)
    {
      uint32_t ret = 0;

      t0  = millis();
      err = swdAlgoWait(&p_flm->algo, p_flm->dev.to_prog, &ret);
      p_tm->call_ms += millis() - t0;
      armed = false;

      if (err != SWD_OK || ret != 0)
      {
        if (err == SWD_OK) err = SWD_ERR_FAULT;
        break;
      }
      done     += armed_len;
      armed_len = 0;
      p_tm->page_cnt++;
      if (cb) cb("program", addr + done, done, len, ctx);
    }

    // 준비된 페이지가 있으면 굽기를 시작만 한다
    if (n > 0)
    {
      t0  = millis();
      err = swdAlgoStart(&p_flm->algo, p_flm->fn_program + (uint32_t)p_flm->delta,
                         addr + done, sz_page, buf[idx], 0);
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
static swd_err_t flmVerifyRange(flm_t *p_flm, uint32_t addr, uint32_t len,
                                flm_fill_t fill, void *src,
                                flm_progress_t cb, void *ctx, uint32_t *p_bad)
{
  static uint8_t  page[FLM_PAGE_MAX] __attribute__((aligned(32)));
  static uint32_t rd[FLM_PAGE_MAX / 4];
  uint32_t        done = 0;
  uint32_t        bad  = 0;
  swd_err_t       err  = SWD_OK;

  while (done < len)
  {
    uint32_t n = p_flm->dev.sz_page;

    if (n > len - done) n = len - done;

    if (fill(src, addr + done, page, n) == false) { err = SWD_ERR_PROTOCOL; break; }

    err = swdMemReadBlock(addr + done, rd, (n + 3) / 4);
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

swd_err_t flmWriteFile(flm_t *p_flm, const char *path, uint32_t addr,
                       flm_progress_t cb, void *ctx, uint32_t *p_written,
                       flm_time_t *p_time)
{
  flm_bin_src_t src;
  flm_time_t    tm;
  FIL           file;
  swd_err_t     err;
  uint32_t      size;
  uint32_t      t0;

  memset(&tm, 0, sizeof(tm));
  if (p_written) *p_written = 0;
  if (p_time)    *p_time    = tm;

  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (flmIsInRange(p_flm, addr) == false)         return SWD_ERR_PROTOCOL;
  if (p_flm->dev.sz_page > FLM_PAGE_MAX)          return SWD_ERR_PROTOCOL;

  if (f_open(&file, path, FA_READ) != FR_OK) return SWD_ERR_PROTOCOL;
  size = (uint32_t)f_size(&file);

  if (size == 0 || flmIsInRange(p_flm, addr + size - 1) == false)
  {
    f_close(&file);
    return SWD_ERR_PROTOCOL;
  }

  src.file  = &file;
  src.base  = addr;
  src.size  = size;
  src.empty = p_flm->dev.val_empty;

  t0  = millis();
  err = flmEraseRange(p_flm, addr, size, cb, ctx);
  tm.erase_ms = millis() - t0;

  if (err == SWD_OK)
  {
    err = flmInit(p_flm, p_flm->dev.dev_adr, FLM_CLK_DEF, FLM_FNC_PROGRAM);
    if (err == SWD_OK)
    {
      err = flmProgramRange(p_flm, addr, size, flmFillBin, &src, cb, ctx, &tm);
      flmUnInit(p_flm, FLM_FNC_PROGRAM);
    }
  }

  f_close(&file);

  if (p_written) *p_written = (uint32_t)tm.page_cnt * p_flm->dev.sz_page;
  if (p_written && *p_written > size) *p_written = size;
  if (p_time)    *p_time    = tm;
  return err;
}

swd_err_t flmVerifyFile(flm_t *p_flm, const char *path, uint32_t addr,
                        flm_progress_t cb, void *ctx, uint32_t *p_bad)
{
  flm_bin_src_t src;
  FIL           file;
  swd_err_t     err;

  if (p_flm == NULL) return SWD_ERR_PROTOCOL;
  if (f_open(&file, path, FA_READ) != FR_OK) return SWD_ERR_PROTOCOL;

  src.file  = &file;
  src.base  = addr;
  src.size  = (uint32_t)f_size(&file);
  src.empty = p_flm->dev.val_empty;

  err = flmVerifyRange(p_flm, addr, src.size, flmFillBin, &src, cb, ctx, p_bad);

  f_close(&file);
  return err;
}


// ----------------------------------------------------------------- ELF 굽기

/* .elf 는 굽는 주소가 파일 안에 있다. PT_LOAD 세그먼트의 p_paddr 이 그것이고,
   .data 처럼 vaddr(RAM)과 paddr(플래시)이 다른 세그먼트가 있으므로 반드시
   paddr 을 봐야 한다. 그래서 .bin 과 달리 주소 인자가 필요 없다. */
static bool flmElfCollect(elf_t *p_elf, flm_elf_src_t *p_src)
{
  elf_phdr_t ph;

  p_src->elf     = p_elf;
  p_src->seg_cnt = 0;

  for (uint32_t i = 0; i < p_elf->e_phnum; i++)
  {
    if (elfGetPhdr(p_elf, i, &ph) == false) continue;
    if (ph.type != ELF_PT_LOAD)             continue;
    if (ph.filesz == 0)                     continue;

    if (p_src->seg_cnt >= FLM_ELF_SEG_MAX) return false;
    p_src->seg[p_src->seg_cnt++] = ph;
  }
  return (p_src->seg_cnt > 0);
}

/* 굽기 범위를 정한다. 시작은 ProgramPage 를 위해 페이지 경계로 내리고,
   앞쪽에 생긴 여백은 fill 이 val_empty 로 채운다. */
static bool flmElfRange(flm_t *p_flm, elf_t *p_elf, uint32_t *p_addr, uint32_t *p_len)
{
  uint32_t lo, hi;

  if (elfGetLoadRange(p_elf, &lo, &hi) == false) return false;
  if (flmIsInRange(p_flm, lo) == false)          return false;
  if (flmIsInRange(p_flm, hi - 1) == false)      return false;

  lo -= (lo - p_flm->dev.dev_adr) % p_flm->dev.sz_page;

  *p_addr = lo;
  *p_len  = hi - lo;
  return true;
}

swd_err_t flmWriteElf(flm_t *p_flm, const char *path,
                      flm_progress_t cb, void *ctx, uint32_t *p_written,
                      flm_time_t *p_time, uint32_t *p_addr)
{
  flm_elf_src_t src;
  flm_time_t    tm;
  elf_t         elf;
  swd_err_t     err;
  uint32_t      addr, len, t0;

  memset(&tm, 0, sizeof(tm));
  if (p_written) *p_written = 0;
  if (p_time)    *p_time    = tm;

  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_flm->dev.sz_page > FLM_PAGE_MAX)          return SWD_ERR_PROTOCOL;

  if (elfOpen(&elf, path) == false) return SWD_ERR_PROTOCOL;

  src.empty = p_flm->dev.val_empty;
  if (flmElfCollect(&elf, &src) == false ||
      flmElfRange(p_flm, &elf, &addr, &len) == false)
  {
    elfClose(&elf);
    return SWD_ERR_PROTOCOL;
  }
  if (p_addr) *p_addr = addr;

  t0  = millis();
  err = flmEraseRange(p_flm, addr, len, cb, ctx);
  tm.erase_ms = millis() - t0;

  if (err == SWD_OK)
  {
    err = flmInit(p_flm, p_flm->dev.dev_adr, FLM_CLK_DEF, FLM_FNC_PROGRAM);
    if (err == SWD_OK)
    {
      err = flmProgramRange(p_flm, addr, len, flmFillElf, &src, cb, ctx, &tm);
      flmUnInit(p_flm, FLM_FNC_PROGRAM);
    }
  }

  elfClose(&elf);

  if (p_written) *p_written = (err == SWD_OK) ? len : 0;
  if (p_time)    *p_time    = tm;
  return err;
}

static bool flmFillHex(void *src, uint32_t addr, uint8_t *p_buf, uint32_t len)
{
  flm_hex_src_t *p_src = (flm_hex_src_t *)src;

  return hexFill(p_src->hex, addr, p_buf, len, p_src->empty);
}

/* .hex 도 주소가 파일 안에 있다. 다만 .elf 처럼 세그먼트로 묶여 있지 않고
   레코드마다 흩어져 있어서, 굽는 범위는 열 때의 전체 스캔으로 얻는다. */
static bool flmHexRange(flm_t *p_flm, hex_t *p_hex, uint32_t *p_addr, uint32_t *p_len)
{
  uint32_t lo = p_hex->lo;

  if (flmIsInRange(p_flm, lo) == false)             return false;
  if (flmIsInRange(p_flm, p_hex->hi - 1) == false)  return false;

  lo -= (lo - p_flm->dev.dev_adr) % p_flm->dev.sz_page;

  *p_addr = lo;
  *p_len  = p_hex->hi - lo;
  return true;
}

swd_err_t flmWriteHex(flm_t *p_flm, const char *path,
                      flm_progress_t cb, void *ctx, uint32_t *p_written,
                      flm_time_t *p_time, uint32_t *p_addr)
{
  static hex_t  hex;
  flm_hex_src_t src;
  flm_time_t    tm;
  swd_err_t     err;
  uint32_t      addr, len, t0;

  memset(&tm, 0, sizeof(tm));
  if (p_written) *p_written = 0;
  if (p_time)    *p_time    = tm;

  if (p_flm == NULL || p_flm->is_loaded == false) return SWD_ERR_PROTOCOL;
  if (p_flm->dev.sz_page > FLM_PAGE_MAX)          return SWD_ERR_PROTOCOL;

  if (hexOpen(&hex, path) == false) return SWD_ERR_PROTOCOL;

  src.hex   = &hex;
  src.empty = p_flm->dev.val_empty;

  if (flmHexRange(p_flm, &hex, &addr, &len) == false)
  {
    hexClose(&hex);
    return SWD_ERR_PROTOCOL;
  }
  if (p_addr) *p_addr = addr;

  t0  = millis();
  err = flmEraseRange(p_flm, addr, len, cb, ctx);
  tm.erase_ms = millis() - t0;

  if (err == SWD_OK)
  {
    err = flmInit(p_flm, p_flm->dev.dev_adr, FLM_CLK_DEF, FLM_FNC_PROGRAM);
    if (err == SWD_OK)
    {
      err = flmProgramRange(p_flm, addr, len, flmFillHex, &src, cb, ctx, &tm);
      flmUnInit(p_flm, FLM_FNC_PROGRAM);
    }
  }

  hexClose(&hex);

  if (p_written) *p_written = (err == SWD_OK) ? len : 0;
  if (p_time)    *p_time    = tm;
  return err;
}

swd_err_t flmVerifyHex(flm_t *p_flm, const char *path,
                       flm_progress_t cb, void *ctx, uint32_t *p_bad)
{
  static hex_t  hex;
  flm_hex_src_t src;
  swd_err_t     err;
  uint32_t      addr, len;

  if (p_flm == NULL) return SWD_ERR_PROTOCOL;
  if (hexOpen(&hex, path) == false) return SWD_ERR_PROTOCOL;

  src.hex   = &hex;
  src.empty = p_flm->dev.val_empty;

  if (flmHexRange(p_flm, &hex, &addr, &len) == false)
  {
    hexClose(&hex);
    return SWD_ERR_PROTOCOL;
  }

  err = flmVerifyRange(p_flm, addr, len, flmFillHex, &src, cb, ctx, p_bad);

  hexClose(&hex);
  return err;
}

swd_err_t flmVerifyElf(flm_t *p_flm, const char *path,
                       flm_progress_t cb, void *ctx, uint32_t *p_bad)
{
  flm_elf_src_t src;
  elf_t         elf;
  swd_err_t     err;
  uint32_t      addr, len;

  if (p_flm == NULL) return SWD_ERR_PROTOCOL;
  if (elfOpen(&elf, path) == false) return SWD_ERR_PROTOCOL;

  src.empty = p_flm->dev.val_empty;
  if (flmElfCollect(&elf, &src) == false ||
      flmElfRange(p_flm, &elf, &addr, &len) == false)
  {
    elfClose(&elf);
    return SWD_ERR_PROTOCOL;
  }

  err = flmVerifyRange(p_flm, addr, len, flmFillElf, &src, cb, ctx, p_bad);

  elfClose(&elf);
  return err;
}


// ----------------------------------------------------------------- 내부

uint32_t flmU16(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

uint32_t flmU32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* DevDscr 섹션에서 FlashDevice 를 읽는다. 타깃과 무관하게 호스트에서만 쓴다. */
bool flmReadDevice(flm_t *p_flm)
{
  elf_sec_t sec;
  uint8_t   hdr[160];
  uint32_t  off;

  if (elfFindSec(&p_flm->elf, FLM_DEVDSCR_NAME, &sec) == false) return false;
  if (sec.size < sizeof(hdr))                                   return false;
  if (elfRead(&p_flm->elf, sec.offset, hdr, sizeof(hdr)) == false) return false;

  p_flm->dev.vers      = (uint16_t)flmU16(&hdr[0]);
  p_flm->dev.dev_type  = (uint16_t)flmU16(&hdr[130]);
  p_flm->dev.dev_adr   = flmU32(&hdr[132]);
  p_flm->dev.sz_dev    = flmU32(&hdr[136]);
  p_flm->dev.sz_page   = flmU32(&hdr[140]);
  p_flm->dev.val_empty = hdr[148];
  p_flm->dev.to_prog   = flmU32(&hdr[152]);
  p_flm->dev.to_erase  = flmU32(&hdr[156]);

  memcpy(p_flm->dev.name, &hdr[2], FLM_NAME_MAX - 1);
  p_flm->dev.name[FLM_NAME_MAX - 1] = 0;

  // 섹터 배열
  off = sec.offset + 160;
  p_flm->dev.sector_cnt = 0;

  for (uint32_t i = 0; i < FLM_SECTOR_MAX; i++)
  {
    uint8_t ent[8];

    if (elfRead(&p_flm->elf, off, ent, sizeof(ent)) == false) break;

    uint32_t size = flmU32(&ent[0]);
    uint32_t addr = flmU32(&ent[4]);

    if (size == 0xFFFFFFFF && addr == 0xFFFFFFFF) break;
    if (size == 0) break;

    p_flm->dev.sector[i].size = size;
    p_flm->dev.sector[i].addr = addr;
    p_flm->dev.sector_cnt++;
    off += 8;
  }

  return (p_flm->dev.sz_page > 0) && (p_flm->dev.sector_cnt > 0);
}

/* 타깃 RAM 으로 흘려보낸다. 섹션 크기도 주소도 4의 배수라는 보장이 없어서
   앞뒤 자투리는 8비트로, 가운데만 블록 전송한다. */
bool flmLoadCb(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx)
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
swd_err_t flmCall(flm_t *p_flm, uint32_t pc,
                  uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                  uint32_t timeout_ms)
{
  swd_err_t err;
  uint32_t  ret = 0;

  if (timeout_ms == 0) timeout_ms = FLM_INIT_TIMEOUT;

  err = swdAlgoCall(&p_flm->algo, pc, r0, r1, r2, r3, timeout_ms, &ret);
  if (err != SWD_OK)
  {
    return err;
  }
  if (ret != 0)
  {
    return SWD_ERR_FAULT;
  }
  return SWD_OK;
}


#endif
