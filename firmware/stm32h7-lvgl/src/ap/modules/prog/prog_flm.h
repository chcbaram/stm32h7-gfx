/*
 * prog_flm.h
 *
 *  CMSIS-Pack 플래시 알고리즘(.FLM) 바인딩.
 *
 *  ELF 로더가 코드를 타깃 RAM 에 올려주면, 여기서 FlashDevice 를 읽어
 *  섹터 배치와 타임아웃을 알아내고 Init / EraseSector / ProgramPage 를
 *  실제로 호출한다.
 *
 *  주의: .FLM 은 성공 시 0 을 반환한다. .stldr 은 1 이다. 극성을 호출부에
 *  흩어놓으면 나중에 .stldr 을 붙일 때 전부 뒤집어야 하므로 여기서 흡수한다.
 */

#ifndef PROG_FLM_H_
#define PROG_FLM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "prog/prog_elf.h"
#include "prog/prog_hex.h"
#include "swd.h"
#include "swd/swd_algo.h"


#ifdef _USE_HW_SWD


#define FLM_SECTOR_MAX      16
#define FLM_NAME_MAX        48
#define FLM_ELF_SEG_MAX     8

// Init/UnInit 의 fnc 인자
#define FLM_FNC_ERASE       1
#define FLM_FNC_PROGRAM     2
#define FLM_FNC_VERIFY      3

// DevType
#define FLM_DEV_ONCHIP      1
#define FLM_DEV_EXTSPI      5


typedef struct
{
  uint32_t size;
  uint32_t addr;        // DevAdr 기준 상대 오프셋
} flm_sector_t;

typedef struct
{
  uint16_t     vers;
  char         name[FLM_NAME_MAX];
  uint16_t     dev_type;
  uint32_t     dev_adr;
  uint32_t     sz_dev;
  uint32_t     sz_page;
  uint8_t      val_empty;
  uint32_t     to_prog;         // ms
  uint32_t     to_erase;        // ms
  flm_sector_t sector[FLM_SECTOR_MAX];
  uint32_t     sector_cnt;
} flm_dev_t;

typedef struct
{
  elf_t          elf;
  flm_dev_t      dev;
  swd_algo_ctx_t algo;

  uint32_t       fn_init;
  uint32_t       fn_uninit;
  uint32_t       fn_erase_chip;
  uint32_t       fn_erase_sector;
  uint32_t       fn_program;

  uint32_t       buf_addr;      // ProgramPage 의 buf 인자로 넘길 타깃 주소
  uint32_t       buf_addr_b;    // 이중 버퍼용 두 번째 버퍼
  uint32_t       ram_base;
  uint32_t       ram_end;
  int32_t        delta;         // 재배치량

  bool           is_open;
  bool           is_loaded;
} flm_t;


// 파일을 열고 FlashDevice 와 심볼을 읽는다. 타깃은 아직 건드리지 않는다.
bool      flmOpen(flm_t *p_flm, const char *path);
void      flmClose(flm_t *p_flm);

// 코드를 타깃 RAM 에 올리고 아레나(트램폴린/스택/버퍼)를 잡는다.
swd_err_t flmLoad(flm_t *p_flm, uint32_t ram_base, uint32_t ram_size);

swd_err_t flmInit(flm_t *p_flm, uint32_t addr, uint32_t clk, uint32_t fnc);
swd_err_t flmUnInit(flm_t *p_flm, uint32_t fnc);
swd_err_t flmEraseSector(flm_t *p_flm, uint32_t addr);
swd_err_t flmEraseChip(flm_t *p_flm);
swd_err_t flmProgramPage(flm_t *p_flm, uint32_t addr, const uint8_t *p_data, uint32_t len);

// 주소가 이 디바이스의 플래시 범위 안인가. 지우기/굽기 전에 반드시 확인한다.
bool      flmIsInRange(flm_t *p_flm, uint32_t addr);
// 주소가 속한 섹터의 시작과 크기. 섹터 배열은 (크기, 오프셋) 쌍이고
// 그 크기가 다음 항목이 나올 때까지 유지된다.
uint32_t  flmSectorBase(flm_t *p_flm, uint32_t addr);
uint32_t  flmSectorSize(flm_t *p_flm, uint32_t addr);

/* 파일 하나를 통째로 굽는다. 필요한 섹터만 지우고 페이지 단위로 쓴다.
   진행 상황은 콜백으로 알린다 (NULL 이면 생략). */
typedef void (*flm_progress_t)(const char *phase, uint32_t addr, uint32_t done,
                               uint32_t total, void *ctx);

/* 어디서 시간을 쓰는지 나눠서 돌려준다. 소거는 순수 타깃 시간이고
   굽기는 우리 오버헤드가 섞여 있어서, 합쳐 보면 개선할 곳이 안 보인다. */
typedef struct
{
  uint32_t erase_ms;
  uint32_t xfer_ms;       // 페이지를 타깃 RAM 버퍼로 옮기는 시간
  uint32_t call_ms;       // ProgramPage 호출(레지스터 세팅 + 완료 대기)
  uint32_t read_ms;       // SD 카드에서 읽는 시간
  uint32_t page_cnt;
  uint32_t sector_cnt;
} flm_time_t;

swd_err_t flmWriteFile(flm_t *p_flm, const char *path, uint32_t addr,
                       flm_progress_t cb, void *ctx, uint32_t *p_written,
                       flm_time_t *p_time);

// 구운 결과를 파일과 되읽어 비교한다.
/* .elf 는 굽는 주소가 파일 안에 있으므로(PT_LOAD 의 p_paddr) addr 인자가 없다.
   실제로 쓴 시작 주소를 p_addr 로 돌려준다. */
swd_err_t flmWriteElf(flm_t *p_flm, const char *path,
                      flm_progress_t cb, void *ctx, uint32_t *p_written,
                      flm_time_t *p_time, uint32_t *p_addr);

swd_err_t flmVerifyElf(flm_t *p_flm, const char *path,
                       flm_progress_t cb, void *ctx, uint32_t *p_bad);

// .hex 도 주소가 파일 안에 있다. 굽는 범위는 hexOpen 의 전체 스캔에서 나온다.
swd_err_t flmWriteHex(flm_t *p_flm, const char *path,
                      flm_progress_t cb, void *ctx, uint32_t *p_written,
                      flm_time_t *p_time, uint32_t *p_addr);

swd_err_t flmVerifyHex(flm_t *p_flm, const char *path,
                       flm_progress_t cb, void *ctx, uint32_t *p_bad);

swd_err_t flmVerifyFile(flm_t *p_flm, const char *path, uint32_t addr,
                        flm_progress_t cb, void *ctx, uint32_t *p_bad);


#endif


#ifdef __cplusplus
}
#endif

#endif
