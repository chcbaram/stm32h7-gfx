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
#include "swd.h"
#include "swd/swd_algo.h"


#ifdef _USE_HW_SWD


#define FLM_SECTOR_MAX      16
#define FLM_NAME_MAX        48

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

swd_err_t flmWriteFile(flm_t *p_flm, const char *path, uint32_t addr,
                       flm_progress_t cb, void *ctx, uint32_t *p_written);

// 구운 결과를 파일과 되읽어 비교한다.
swd_err_t flmVerifyFile(flm_t *p_flm, const char *path, uint32_t addr,
                        flm_progress_t cb, void *ctx, uint32_t *p_bad);


#endif


#ifdef __cplusplus
}
#endif

#endif
