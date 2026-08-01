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

  p_flm->buf_addr = (p_flm->algo.stack_top + 3) & ~3UL;

  /* PrgData 가 있으면 그 주소가 R9(static base)다. RWPI 로 빌드된 알고리즘은
     전역 접근에 R9 를 쓴다. */
  {
    elf_sec_t sec;

    if (elfFindSec(&p_flm->elf, "PrgData", &sec) == true && sec.size > 0)
    {
      p_flm->algo.static_base = (uint32_t)((int32_t)sec.addr + p_flm->delta);
    }
  }

  if (p_flm->buf_addr + p_flm->dev.sz_page > p_flm->ram_end)
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
  if (flmLoadCb(p_flm->buf_addr, p_data, len, p_flm) == false)
  {
    return SWD_ERR_FAULT;
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

swd_err_t flmWriteFile(flm_t *p_flm, const char *path, uint32_t addr,
                       flm_progress_t cb, void *ctx, uint32_t *p_written)
{
  static uint8_t page[FLM_PAGE_MAX] __attribute__((aligned(32)));
  FIL        file;
  swd_err_t  err;
  uint32_t   size;
  uint32_t   done = 0;

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

  err = flmEraseRange(p_flm, addr, size, cb, ctx);
  if (err != SWD_OK)
  {
    f_close(&file);
    return err;
  }

  err = flmInit(p_flm, p_flm->dev.dev_adr, FLM_CLK_DEF, FLM_FNC_PROGRAM);
  if (err != SWD_OK)
  {
    f_close(&file);
    return err;
  }

  while (done < size)
  {
    uint32_t n = p_flm->dev.sz_page;
    UINT     br = 0;

    if (n > size - done) n = size - done;

    if (f_read(&file, page, n, &br) != FR_OK || br != n)
    {
      err = SWD_ERR_PROTOCOL;
      break;
    }
    // 마지막 페이지가 짧으면 빈 값으로 채운다
    if (n < p_flm->dev.sz_page)
    {
      memset(&page[n], p_flm->dev.val_empty, p_flm->dev.sz_page - n);
      n = p_flm->dev.sz_page;
    }

    err = flmProgramPage(p_flm, addr + done, page, n);
    if (err != SWD_OK) break;

    done += br;
    if (cb) cb("program", addr + done, done, size, ctx);
  }

  flmUnInit(p_flm, FLM_FNC_PROGRAM);
  f_close(&file);

  if (p_written) *p_written = done;
  return err;
}

swd_err_t flmVerifyFile(flm_t *p_flm, const char *path, uint32_t addr,
                        flm_progress_t cb, void *ctx, uint32_t *p_bad)
{
  static uint8_t  page[FLM_PAGE_MAX] __attribute__((aligned(32)));
  static uint32_t rd[FLM_PAGE_MAX / 4];
  FIL       file;
  uint32_t  size, done = 0, bad = 0;
  swd_err_t err = SWD_OK;

  if (p_flm == NULL) return SWD_ERR_PROTOCOL;

  if (f_open(&file, path, FA_READ) != FR_OK) return SWD_ERR_PROTOCOL;
  size = (uint32_t)f_size(&file);

  while (done < size)
  {
    uint32_t n = p_flm->dev.sz_page;
    UINT     br = 0;

    if (n > size - done) n = size - done;
    if (f_read(&file, page, n, &br) != FR_OK || br != n) { err = SWD_ERR_PROTOCOL; break; }

    err = swdMemReadBlock(addr + done, rd, (n + 3) / 4);
    if (err != SWD_OK) break;

    for (uint32_t i = 0; i < n; i++)
    {
      if (((uint8_t *)rd)[i] != page[i]) bad++;
    }
    done += n;
    if (cb) cb("verify", addr + done, done, size, ctx);
  }

  f_close(&file);
  if (p_bad) *p_bad = bad;
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
