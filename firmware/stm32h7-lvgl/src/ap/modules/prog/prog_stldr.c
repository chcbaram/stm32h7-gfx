/*
 * prog_stldr.c
 *
 *  ST 로더(.stldr) 구현
 *
 *  StorageInfo 구조체 배치 (ST Dev_Inf.h, 200바이트)
 *      off  size
 *        0  100   DeviceName
 *      100    2   DeviceType  (+2 패딩)
 *      104    4   DeviceStartAddress
 *      108    4   DeviceSize
 *      112    4   PageSize
 *      116    1   EraseValue  (+3 패딩)
 *      120  8×10  { SectorNum, SectorSize }  {0,0} 으로 끝
 *
 *  섹터 배열의 의미가 .FLM 과 반대다. 여기는 (개수, 크기) 런이고 .FLM 은
 *  (크기, 오프셋) 이다. 공통 계층 표현으로 펼쳐서 담는다.
 *
 *  호출 규약 (디스어셈블로 확인)
 *      Init(void)                              -> 1 이면 성공
 *      SectorErase(start, end, psize)          -> 1
 *      MassErase(psize)                        -> 1
 *      Write(addr, size, buf, psize)           -> 1
 *
 *  뒤쪽 psize 인자는 FlashLoader 계열만 쓴다. ExternalLoader 는 그 자리를 안
 *  보는데, AAPCS 상 남는 레지스터는 무시되므로 양쪽에 그냥 넘겨도 된다.
 */

#include "prog/prog_stldr.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"


#ifdef _USE_HW_SWD


#define STLDR_INFO_NAME     "StorageInfo"
#define STLDR_INFO_SIZE     200
#define STLDR_STACK_SIZE    0x4000
#define STLDR_INIT_TIMEOUT  10000
#define STLDR_ERASE_TIMEOUT 30000
#define STLDR_CHIP_TIMEOUT  120000
#define STLDR_PROG_TIMEOUT  5000


static bool      stldrProbe(elf_t *p_elf);
static bool      stldrParse(algo_t *p_algo);
static swd_err_t stldrLoadCode(algo_t *p_algo, uint32_t ram_base, uint32_t ram_size);
static swd_err_t stldrInit(algo_t *p_algo, uint32_t fnc);
static swd_err_t stldrUnInit(algo_t *p_algo, uint32_t fnc);
static swd_err_t stldrEraseSector(algo_t *p_algo, uint32_t addr, uint32_t size);
static swd_err_t stldrEraseChip(algo_t *p_algo);
static swd_err_t stldrProgStart(algo_t *p_algo, uint32_t addr, uint32_t len, uint32_t buf);
static swd_err_t stldrProgWait(algo_t *p_algo, uint32_t timeout_ms);

static uint32_t  stldrU16(const uint8_t *p);
static uint32_t  stldrU32(const uint8_t *p);
static bool      stldrReadInfo(algo_t *p_algo, uint32_t vaddr);
static swd_err_t stldrCall(algo_t *p_algo, uint32_t pc,
                           uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                           uint32_t timeout_ms);


const algo_ops_t stldr_ops =
{
  .name         = "stldr",
  .probe        = stldrProbe,
  .parse        = stldrParse,
  .load         = stldrLoadCode,
  .init         = stldrInit,
  .uninit       = stldrUnInit,
  .erase_sector = stldrEraseSector,
  .erase_chip   = stldrEraseChip,
  .prog_start   = stldrProgStart,
  .prog_wait    = stldrProgWait,
};


// ----------------------------------------------------------------- 판별/파싱

static bool stldrProbe(elf_t *p_elf)
{
  uint32_t addr;

  return elfFindSym(p_elf, STLDR_INFO_NAME, &addr);
}

static bool stldrParse(algo_t *p_algo)
{
  uint32_t info_addr = 0;

  if (elfFindSym(&p_algo->elf, "Init",  &p_algo->fn_init)    == false ||
      elfFindSym(&p_algo->elf, "Write", &p_algo->fn_program) == false)
  {
    return false;
  }
  elfFindSym(&p_algo->elf, "SectorErase", &p_algo->fn_erase_sector);
  elfFindSym(&p_algo->elf, "MassErase",   &p_algo->fn_erase_chip);

  if (elfFindSym(&p_algo->elf, STLDR_INFO_NAME, &info_addr) == false) return false;
  if (stldrReadInfo(p_algo, info_addr) == false)                      return false;

  /* Write 가 임의 길이를 받으므로 조각 크기를 우리가 고른다. StorageInfo 의
     PageSize 는 F411 의 경우 16KB 라 그대로 쓰면 버퍼가 감당이 안 되고,
     QSPI 는 256B 라 너무 잘게 쪼개진다. */
  p_algo->buf_size = ALGO_BUF_MAX;
  p_algo->kind     = ALGO_KIND_STLDR;
  p_algo->delta    = 0;                 // 절대 주소. 재배치하지 않는다.

  return (p_algo->dev.sz_dev > 0) && (p_algo->dev.sector_cnt > 0);
}


// ----------------------------------------------------------------- 로드

/* 링크된 주소 그대로 올린다. 재배치하면 Init 안에서 하드폴트가 나고 진단이
   안 나온다.

   vaddr 이 0 근처인 세그먼트는 올리지 않는다. StorageInfo 만 담은 기술자
   세그먼트를 링커가 주소 0 에 놓은 경우가 많은데(CubeProgrammer 1.22 기준
   FlashLoader 96개 중 70개), PT_LOAD 라서 그대로 올리면 타깃 주소 0 에 쓰려
   든다. .FLM 의 DevDscr 과 같은 성격이다. */
static bool stldrLoadSegCb(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx)
{
  if (addr < 0x1000) return true;       // 호스트용 기술자. 건너뛴다.

  return algoWriteMem(addr, p_data, len, ctx);
}

static swd_err_t stldrLoadCode(algo_t *p_algo, uint32_t ram_base, uint32_t ram_size)
{
  swd_err_t err;
  uint32_t  lo = 0, hi = 0;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  ram_base = (ram_base + 7) & ~7UL;
  p_algo->ram_base = ram_base;
  p_algo->ram_end  = ram_base + ram_size;

  if (elfLoadSegments(&p_algo->elf, stldrLoadSegCb, p_algo, &lo, &hi) == false)
  {
    return SWD_ERR_FAULT;
  }
  if (hi <= 0x1000) return SWD_ERR_PROTOCOL;    // 올릴 코드가 없다

  /* 로더가 자기 주소를 고정으로 갖고 있으므로 아레나를 그 뒤에 잡는다.
     트램폴린도 여기 둔다 — ram_base 앞머리는 로더가 이미 쓰고 있을 수 있다.

     스택은 넉넉히 준다. 스택은 stack_top 에서 아래로 자라므로 모자라면 바로
     아래에 있는 로더의 .bss 를 덮어쓴다. 그러면 로더가 "왜인지 초기화에
     실패" 하는데, 자기 변수가 깨진 것이라 진단이 안 나온다.

     실제로 이걸로 하루를 썼다. W25Q128JV 외부 로더가 Init 에서 실패했는데,
     같은 함수들을 멀리 떨어진 스택으로 하나씩 부르면 전부 성공했다. .FLM 은
     하는 일이 적어 2KB 로도 되지만 .stldr 은 HAL 로 클럭과 XSPI 를 다 세우기
     때문에 훨씬 많이 쓴다. */
  {
    uint32_t arena = (hi + 7) & ~7UL;

    p_algo->algo.code_addr   = lo;
    p_algo->algo.bkpt_addr   = arena + SWD_ALGO_BKPT_OFFS;
    p_algo->algo.static_base = 0;
    p_algo->algo.stack_top   = (arena + SWD_ALGO_CODE_OFFS + STLDR_STACK_SIZE + 7) & ~7UL;

    err = swdMemWrite32(p_algo->algo.bkpt_addr, SWD_ALGO_BKPT_WORD);
    if (err != SWD_OK) return err;

    p_algo->buf_addr   = (p_algo->algo.stack_top + 3) & ~3UL;
    p_algo->buf_addr_b = p_algo->buf_addr + ((p_algo->buf_size + 3) & ~3UL);
  }

  /* 로더가 DB 가 아는 RAM 영역 안에 자리를 잡았을 때만 넘침을 검사한다.

     .stldr 은 자기 주소를 갖고 있고, 그게 DB 의 ram 과 다른 영역일 수 있다.
     STM32H7S3 이 그렇다 - DB 는 DTCM(0x20000000)을 알려주는데 이 보드의 외부
     로더는 AXI SRAM(0x24000004)에 링크되어 있다. ST 의 내부 플래시 로더는
     DTCM 을 쓰므로 둘 다 맞다.

     이때는 검사할 근거가 없다. 로더를 만든 사람이 그 보드의 RAM 을 알고 고른
     주소이므로 그대로 믿되, 검사를 건너뛰었다는 것은 알린다. */
  if (lo >= p_algo->ram_base && lo < p_algo->ram_end)
  {
    while (p_algo->buf_addr_b + p_algo->buf_size > p_algo->ram_end &&
           p_algo->buf_size > ALGO_BUF_MIN)
    {
      p_algo->buf_size  /= 2;
      p_algo->buf_addr_b = p_algo->buf_addr + ((p_algo->buf_size + 3) & ~3UL);
    }
    if (p_algo->buf_addr_b + p_algo->buf_size > p_algo->ram_end)
    {
      return SWD_ERR_PROTOCOL;
    }
  }

  p_algo->is_loaded = true;
  return SWD_OK;
}


// ----------------------------------------------------------------- 알고리즘 호출

/* .stldr 의 Init 은 인자가 없다. 소거인지 굽기인지 구분하지 않으므로 fnc 는
   버린다. 두 번 불러도 문제되지 않는다 (unlock + 플래그 클리어일 뿐이다). */
static swd_err_t stldrInit(algo_t *p_algo, uint32_t fnc)
{
  (void)fnc;

  if (p_algo->is_loaded == false) return SWD_ERR_PROTOCOL;

  return stldrCall(p_algo, p_algo->fn_init, 0, 0, 0, 0, STLDR_INIT_TIMEOUT);
}

// UnInit 이 없다. 공통 계층이 부르므로 자리만 채운다.
static swd_err_t stldrUnInit(algo_t *p_algo, uint32_t fnc)
{
  (void)p_algo;
  (void)fnc;

  return SWD_OK;
}

/* SectorErase 는 [start, end] 범위를 받아 내부에서 섹터를 훑는다. 한 번에
   전 범위를 넘길 수도 있지만 섹터 단위로 부른다 — 진행 상황이 보이고
   타임아웃도 한 섹터분으로 좁아진다. */
static swd_err_t stldrEraseSector(algo_t *p_algo, uint32_t addr, uint32_t size)
{
  if (p_algo->is_loaded == false)           return SWD_ERR_PROTOCOL;
  if (p_algo->fn_erase_sector == 0)         return SWD_ERR_PROTOCOL;
  if (algoIsInRange(p_algo, addr) == false) return SWD_ERR_PROTOCOL;
  if (size == 0)                            return SWD_ERR_PROTOCOL;

  return stldrCall(p_algo, p_algo->fn_erase_sector,
                   addr, addr + size - 1, p_algo->psize, 0, STLDR_ERASE_TIMEOUT);
}

static swd_err_t stldrEraseChip(algo_t *p_algo)
{
  if (p_algo->is_loaded == false)   return SWD_ERR_PROTOCOL;
  if (p_algo->fn_erase_chip == 0)   return SWD_ERR_PROTOCOL;

  return stldrCall(p_algo, p_algo->fn_erase_chip,
                   p_algo->psize, 0, 0, 0, STLDR_CHIP_TIMEOUT);
}

static swd_err_t stldrProgStart(algo_t *p_algo, uint32_t addr, uint32_t len, uint32_t buf)
{
  if (p_algo->is_loaded == false)           return SWD_ERR_PROTOCOL;
  if (algoIsInRange(p_algo, addr) == false) return SWD_ERR_PROTOCOL;

  return swdAlgoStart(&p_algo->algo, p_algo->fn_program,
                      addr, len, buf, p_algo->psize);
}

static swd_err_t stldrProgWait(algo_t *p_algo, uint32_t timeout_ms)
{
  swd_err_t err;
  uint32_t  ret = 0;

  if (timeout_ms == 0) timeout_ms = STLDR_PROG_TIMEOUT;

  err = swdAlgoWait(&p_algo->algo, timeout_ms, &ret);
  if (err != SWD_OK) return err;

  return (ret != 1) ? SWD_ERR_FAULT : SWD_OK;   // .stldr 은 1 이 성공
}


// ----------------------------------------------------------------- 내부

static bool stldrReadInfo(algo_t *p_algo, uint32_t vaddr)
{
  uint8_t    info[STLDR_INFO_SIZE];
  elf_phdr_t ph;
  bool       found = false;
  uint32_t   dev_type;
  uint32_t   off = 0;

  /* StorageInfo 는 타깃에 안 올라갈 수도 있으므로 파일에서 직접 읽는다.

     세그먼트가 겹치는 파일이 있다. 직접 만든 로더에서 봤는데, 코드 세그먼트가
     기술자 주소까지 덮고 있고 그 자리는 0 인 반면 진짜 값은 뒤에 따로 놓인
     작은 세그먼트에 있었다. 로더는 순서대로 올리므로 나중 것이 이긴다 —
     그래서 처음 맞는 걸 잡지 말고 끝까지 훑어 마지막 것을 쓴다. */
  for (uint32_t i = 0; i < p_algo->elf.e_phnum; i++)
  {
    if (elfGetPhdr(&p_algo->elf, i, &ph) == false) continue;
    if (ph.type != ELF_PT_LOAD)                    continue;
    if (vaddr < ph.vaddr || vaddr >= ph.vaddr + ph.filesz) continue;

    off   = ph.offset + (vaddr - ph.vaddr);
    found = true;
  }
  if (found == false)                                        return false;
  if (elfRead(&p_algo->elf, off, info, sizeof(info)) == false) return false;

  memcpy(p_algo->dev.name, info, ALGO_NAME_MAX - 1);
  p_algo->dev.name[ALGO_NAME_MAX - 1] = 0;

  dev_type              = stldrU16(&info[100]);
  p_algo->dev.dev_type  = (dev_type == STLDR_DEV_MCU_FLASH) ? ALGO_DEV_ONCHIP
                                                            : ALGO_DEV_EXTERNAL;
  p_algo->dev.dev_adr   = stldrU32(&info[104]);
  p_algo->dev.sz_dev    = stldrU32(&info[108]);
  p_algo->dev.sz_page   = stldrU32(&info[112]);
  p_algo->dev.val_empty = info[116];

  // StorageInfo 에는 타임아웃이 없다. 넉넉히 잡는다.
  p_algo->dev.to_prog  = STLDR_PROG_TIMEOUT;
  p_algo->dev.to_erase = STLDR_ERASE_TIMEOUT;

  /* 섹터 배열이 (개수, 크기) 런이다. 공통 계층은 (크기, 오프셋) 을 쓰므로
     펼쳐서 담는다. 같은 크기가 연속이면 한 항목으로 묶인다 — 공통 계층이
     "다음 항목 전까지 이 크기가 유지된다" 로 읽기 때문이다. */
  p_algo->dev.sector_cnt = 0;
  {
    uint32_t offs = 0;

    for (uint32_t i = 0; i < 10; i++)
    {
      uint32_t num = stldrU32(&info[120 + i * 8]);
      uint32_t sz  = stldrU32(&info[124 + i * 8]);

      if (num == 0 || sz == 0) break;
      if (p_algo->dev.sector_cnt >= ALGO_SECTOR_MAX) return false;

      p_algo->dev.sector[p_algo->dev.sector_cnt].size = sz;
      p_algo->dev.sector[p_algo->dev.sector_cnt].addr = offs;
      p_algo->dev.sector_cnt++;
      offs += num * sz;
    }
  }

  return true;
}

static uint32_t stldrU16(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t stldrU32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 성공 코드 극성(.stldr 은 1)을 여기 한 곳에 가둔다.
static swd_err_t stldrCall(algo_t *p_algo, uint32_t pc,
                           uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                           uint32_t timeout_ms)
{
  swd_err_t err;
  uint32_t  ret = 0;

  if (timeout_ms == 0) timeout_ms = STLDR_INIT_TIMEOUT;

  err = swdAlgoCall(&p_algo->algo, pc, r0, r1, r2, r3, timeout_ms, &ret);
  if (err != SWD_OK) return err;

  return (ret != 1) ? SWD_ERR_FAULT : SWD_OK;
}


#endif
