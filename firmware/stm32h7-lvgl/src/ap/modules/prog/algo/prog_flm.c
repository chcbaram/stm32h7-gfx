/*
 * prog_flm.c
 *
 *  CMSIS-Pack 플래시 알고리즘(.FLM) 구현
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

#include "prog/algo/prog_flm.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"


#ifdef _USE_HW_SWD


#define FLM_DEVDSCR_NAME    "DevDscr"
#define FLM_STACK_SIZE      0x800
#define FLM_CODE_OFFS       SWD_ALGO_CODE_OFFS
#define FLM_INIT_TIMEOUT    2000
#define FLM_CHIP_TIMEOUT    60000
#define FLM_CLK_DEF         8000000


static bool      flmProbe(elf_t *p_elf);
static bool      flmParse(algo_t *p_algo);
static swd_err_t flmLoadCode(algo_t *p_algo, uint32_t ram_base, uint32_t ram_size);
static swd_err_t flmInit(algo_t *p_algo, uint32_t fnc);
static swd_err_t flmUnInit(algo_t *p_algo, uint32_t fnc);
static swd_err_t flmEraseSector(algo_t *p_algo, uint32_t addr, uint32_t size);
static swd_err_t flmEraseChip(algo_t *p_algo);
static swd_err_t flmProgStart(algo_t *p_algo, uint32_t addr, uint32_t len, uint32_t buf);
static swd_err_t flmProgWait(algo_t *p_algo, uint32_t timeout_ms);

static uint32_t  flmU16(const uint8_t *p);
static uint32_t  flmU32(const uint8_t *p);
static swd_err_t flmCall(algo_t *p_algo, uint32_t pc,
                         uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                         uint32_t timeout_ms);


const algo_ops_t flm_ops =
{
  .name         = "FLM",
  .probe        = flmProbe,
  .parse        = flmParse,
  .load         = flmLoadCode,
  .init         = flmInit,
  .uninit       = flmUnInit,
  .erase_sector = flmEraseSector,
  .erase_chip   = flmEraseChip,
  .prog_start   = flmProgStart,
  .prog_wait    = flmProgWait,
};


// ----------------------------------------------------------------- 판별/파싱

/* DevDscr 섹션이 있으면 .FLM 이다. 확장자를 보지 않는다. */
static bool flmProbe(elf_t *p_elf)
{
  return elfFindSec(p_elf, FLM_DEVDSCR_NAME, NULL);
}

static bool flmParse(algo_t *p_algo)
{
  elf_sec_t sec;
  uint8_t   hdr[160];
  uint32_t  off;
  uint32_t  dev_type;

  if (elfFindSym(&p_algo->elf, "Init",        &p_algo->fn_init)    == false ||
      elfFindSym(&p_algo->elf, "UnInit",      &p_algo->fn_uninit)  == false ||
      elfFindSym(&p_algo->elf, "ProgramPage", &p_algo->fn_program) == false)
  {
    return false;
  }
  elfFindSym(&p_algo->elf, "EraseSector", &p_algo->fn_erase_sector);
  elfFindSym(&p_algo->elf, "EraseChip",   &p_algo->fn_erase_chip);

  if (elfFindSec(&p_algo->elf, FLM_DEVDSCR_NAME, &sec) == false)     return false;
  if (sec.size < sizeof(hdr))                                        return false;
  if (elfRead(&p_algo->elf, sec.offset, hdr, sizeof(hdr)) == false)  return false;

  dev_type              = flmU16(&hdr[130]);
  p_algo->dev.dev_type  = (dev_type == FLM_DEV_ONCHIP) ? ALGO_DEV_ONCHIP : ALGO_DEV_EXTERNAL;
  p_algo->dev.dev_adr   = flmU32(&hdr[132]);
  p_algo->dev.sz_dev    = flmU32(&hdr[136]);
  p_algo->dev.sz_page   = flmU32(&hdr[140]);
  p_algo->dev.val_empty = hdr[148];
  p_algo->dev.to_prog   = flmU32(&hdr[152]);
  p_algo->dev.to_erase  = flmU32(&hdr[156]);

  memcpy(p_algo->dev.name, &hdr[2], ALGO_NAME_MAX - 1);
  p_algo->dev.name[ALGO_NAME_MAX - 1] = 0;

  // 섹터 배열. (크기, 오프셋) 쌍이라 공통 계층의 표현과 그대로 같다.
  off = sec.offset + 160;
  p_algo->dev.sector_cnt = 0;

  for (uint32_t i = 0; i < ALGO_SECTOR_MAX; i++)
  {
    uint8_t  ent[8];
    uint32_t size;
    uint32_t addr;

    if (elfRead(&p_algo->elf, off, ent, sizeof(ent)) == false) break;

    size = flmU32(&ent[0]);
    addr = flmU32(&ent[4]);

    if (size == 0xFFFFFFFF && addr == 0xFFFFFFFF) break;
    if (size == 0) break;

    p_algo->dev.sector[i].size = size;
    p_algo->dev.sector[i].addr = addr;
    p_algo->dev.sector_cnt++;
    off += 8;
  }

  /* ProgramPage 는 szPage 단위로 부르는 게 규약이라 조각 크기를 못 고른다.
     .stldr 은 임의 길이를 받아서 우리가 정한다. */
  p_algo->dev.sz_page = p_algo->dev.sz_page;
  p_algo->buf_size    = p_algo->dev.sz_page;
  p_algo->kind        = ALGO_KIND_FLM;

  return (p_algo->dev.sz_page > 0) &&
         (p_algo->dev.sz_page <= ALGO_BUF_MAX) &&
         (p_algo->dev.sector_cnt > 0);
}


// ----------------------------------------------------------------- 로드

/* .FLM 은 보통 0x00000000 에 링크된 ROPI 라 타깃 RAM 으로 옮겨 올린다.
   DevDscr 은 호스트만 읽는 메타데이터라 타깃에 올리지 않는다. */
static swd_err_t flmLoadCode(algo_t *p_algo, uint32_t ram_base, uint32_t ram_size)
{
  swd_err_t err;
  uint32_t  base = 0;
  uint32_t  lo = 0, hi = 0;
  uint32_t  code_addr;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  ram_base = (ram_base + 7) & ~7UL;
  p_algo->ram_base = ram_base;
  p_algo->ram_end  = ram_base + ram_size;

  /* 아레나 앞머리는 알고리즘 러너가 쓰는 BKPT 트램폴린이다.
     코드는 그 뒤부터 올린다. */
  code_addr = ram_base + FLM_CODE_OFFS;

  if (elfGetAllocBase(&p_algo->elf, FLM_DEVDSCR_NAME, &base) == false)
  {
    return SWD_ERR_PROTOCOL;
  }
  p_algo->delta = (int32_t)code_addr - (int32_t)base;

  err = swdMemWrite32(ram_base + SWD_ALGO_BKPT_OFFS, SWD_ALGO_BKPT_WORD);
  if (err != SWD_OK) return err;

  if (elfLoadSections(&p_algo->elf, p_algo->delta, FLM_DEVDSCR_NAME,
                      algoWriteMem, p_algo, &lo, &hi) == false)
  {
    return SWD_ERR_FAULT;
  }

  // 스택은 로드된 영역 뒤에, 버퍼는 다시 그 뒤에 둔다
  p_algo->algo.code_addr   = code_addr;
  p_algo->algo.bkpt_addr   = ram_base + SWD_ALGO_BKPT_OFFS;
  p_algo->algo.static_base = 0;
  p_algo->algo.stack_top   = (hi + FLM_STACK_SIZE + 7) & ~7UL;

  /* 버퍼를 두 개 잡는다. 타깃이 A 를 굽는 동안 B 를 채워 넣기 위해서다. */
  p_algo->buf_addr   = (p_algo->algo.stack_top + 3) & ~3UL;
  p_algo->buf_addr_b = p_algo->buf_addr + ((p_algo->buf_size + 3) & ~3UL);

  /* PrgData 가 있으면 그 주소가 R9(static base)다. RWPI 로 빌드된 알고리즘은
     전역 접근에 R9 를 쓴다. */
  {
    elf_sec_t sec;

    if (elfFindSec(&p_algo->elf, "PrgData", &sec) == true && sec.size > 0)
    {
      p_algo->algo.static_base = (uint32_t)((int32_t)sec.addr + p_algo->delta);
    }
  }

  if (p_algo->buf_addr_b + p_algo->buf_size > p_algo->ram_end)
  {
    return SWD_ERR_PROTOCOL;      // 아레나가 RAM 을 넘는다
  }

  p_algo->is_loaded = true;
  return SWD_OK;
}


// ----------------------------------------------------------------- 알고리즘 호출

static swd_err_t flmInit(algo_t *p_algo, uint32_t fnc)
{
  if (p_algo->is_loaded == false) return SWD_ERR_PROTOCOL;

  return flmCall(p_algo, p_algo->fn_init + (uint32_t)p_algo->delta,
                 p_algo->dev.dev_adr, FLM_CLK_DEF, fnc, 0, FLM_INIT_TIMEOUT);
}

static swd_err_t flmUnInit(algo_t *p_algo, uint32_t fnc)
{
  if (p_algo->is_loaded == false) return SWD_ERR_PROTOCOL;

  return flmCall(p_algo, p_algo->fn_uninit + (uint32_t)p_algo->delta,
                 fnc, 0, 0, 0, FLM_INIT_TIMEOUT);
}

/* .FLM 은 섹터를 하나씩 지운다 (.stldr 은 범위를 통째로 받는다).
   size 는 여기서 쓰지 않는다. */
static swd_err_t flmEraseSector(algo_t *p_algo, uint32_t addr, uint32_t size)
{
  (void)size;

  if (p_algo->is_loaded == false)     return SWD_ERR_PROTOCOL;
  if (p_algo->fn_erase_sector == 0)   return SWD_ERR_PROTOCOL;
  if (algoIsInRange(p_algo, addr) == false) return SWD_ERR_PROTOCOL;

  return flmCall(p_algo, p_algo->fn_erase_sector + (uint32_t)p_algo->delta,
                 addr, 0, 0, 0, p_algo->dev.to_erase);
}

static swd_err_t flmEraseChip(algo_t *p_algo)
{
  if (p_algo->is_loaded == false)   return SWD_ERR_PROTOCOL;
  if (p_algo->fn_erase_chip == 0)   return SWD_ERR_PROTOCOL;

  return flmCall(p_algo, p_algo->fn_erase_chip + (uint32_t)p_algo->delta,
                 0, 0, 0, 0, FLM_CHIP_TIMEOUT);
}

static swd_err_t flmProgStart(algo_t *p_algo, uint32_t addr, uint32_t len, uint32_t buf)
{
  if (p_algo->is_loaded == false)           return SWD_ERR_PROTOCOL;
  if (algoIsInRange(p_algo, addr) == false) return SWD_ERR_PROTOCOL;

  return swdAlgoStart(&p_algo->algo, p_algo->fn_program + (uint32_t)p_algo->delta,
                      addr, len, buf, 0);
}

static swd_err_t flmProgWait(algo_t *p_algo, uint32_t timeout_ms)
{
  swd_err_t err;
  uint32_t  ret = 0;

  err = swdAlgoWait(&p_algo->algo, timeout_ms, &ret);
  if (err != SWD_OK) return err;

  return (ret != 0) ? SWD_ERR_FAULT : SWD_OK;    // .FLM 은 0 이 성공
}


// ----------------------------------------------------------------- 내부

static uint32_t flmU16(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t flmU32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 성공 코드 극성을 여기 한 곳에 가둔다. 호출부에 흩어놓으면 .stldr 을 붙일 때
   전부 뒤집어야 하고, 한 군데만 놓쳐도 실패를 성공으로 보고한다. */
static swd_err_t flmCall(algo_t *p_algo, uint32_t pc,
                         uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                         uint32_t timeout_ms)
{
  swd_err_t err;
  uint32_t  ret = 0;

  if (timeout_ms == 0) timeout_ms = FLM_INIT_TIMEOUT;

  err = swdAlgoCall(&p_algo->algo, pc, r0, r1, r2, r3, timeout_ms, &ret);
  if (err != SWD_OK) return err;

  return (ret != 0) ? SWD_ERR_FAULT : SWD_OK;
}


#endif
