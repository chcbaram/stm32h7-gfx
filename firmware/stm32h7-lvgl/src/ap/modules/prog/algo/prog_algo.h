/*
 * prog_algo.h
 *
 *  플래시 알고리즘 공통 계층.
 *
 *  이 파일 아래로는 벤더도 파일 포맷도 모른다. "지울 섹터가 여기 있고, 이
 *  주소에 이만큼 써라" 까지만 안다. 실제 호출 규약은 아래 두 구현이 채운다.
 *
 *    prog_flm.c    CMSIS-Pack .FLM  — ARM 표준. Nordic/NXP/Renesas/Infineon 이
 *                                     전부 배포하므로 벤더 중립 기본 경로다.
 *    prog_stldr.c  ST .stldr        — ST 전용. 내부 플래시(FlashLoader)는
 *                                     소거 병렬도를 받아 .FLM 보다 빠르고,
 *                                     외부 메모리(ExternalLoader)도 여기로 온다.
 *
 *  새 벤더가 늘어도 이 파일은 그대로다. 늘어나는 건 .FLM 파일과 디바이스 DB
 *  항목뿐이고, 새 구현이 필요한 건 새로운 "알고리즘 파일 포맷" 이 나올 때다.
 */

#ifndef PROG_ALGO_H_
#define PROG_ALGO_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "prog/image/prog_elf.h"
#include "prog/image/prog_hex.h"
#include "swd.h"
#include "swd/swd_algo.h"


#ifdef _USE_HW_SWD


#define ALGO_SECTOR_MAX     16
#define ALGO_NAME_MAX       48
#define ALGO_ELF_SEG_MAX    8
#define ALGO_BUF_MAX        4096      // 이중 버퍼 하나의 최대 크기
#define ALGO_BUF_MIN        256

// Init/UnInit 의 fnc 인자 (.FLM 규약. .stldr 은 무시한다)
#define ALGO_FNC_ERASE      1
#define ALGO_FNC_PROGRAM    2
#define ALGO_FNC_VERIFY     3

/* 소거·굽기 병렬도. .stldr 이 인자로 받는다 (0=x8 1=x16 2=x32 3=x64).
   전압이 높을수록 큰 단위를 쓸 수 있고 그만큼 빨라진다. .FLM 에는 이걸 알릴
   통로가 없어서 알고리즘이 정한 값을 그대로 쓴다. */
#define ALGO_PSIZE_8        0
#define ALGO_PSIZE_16       1
#define ALGO_PSIZE_32       2
#define ALGO_PSIZE_64       3

// 디바이스 종류 (.FLM DevType 과 .stldr DeviceType 을 여기로 정규화한다)
#define ALGO_DEV_ONCHIP     1
#define ALGO_DEV_EXTERNAL   2


typedef enum
{
  ALGO_KIND_NONE = 0,
  ALGO_KIND_FLM,          // CMSIS-Pack .FLM
  ALGO_KIND_STLDR,        // ST External Loader / FlashLoader
} algo_kind_t;

/* 섹터 배치. 두 포맷의 인코딩이 다르지만(.FLM 은 (크기, 오프셋),
   .stldr 은 (개수, 크기)) 여기로 펼쳐 담아서 위쪽은 구분하지 않는다. */
typedef struct
{
  uint32_t size;
  uint32_t addr;          // dev_adr 기준 상대 오프셋
} algo_sector_t;

typedef struct
{
  char          name[ALGO_NAME_MAX];
  uint16_t      dev_type;       // ALGO_DEV_*
  uint32_t      dev_adr;
  uint32_t      sz_dev;
  uint32_t      sz_page;
  uint8_t       val_empty;
  uint32_t      to_prog;        // ms
  uint32_t      to_erase;       // ms
  algo_sector_t sector[ALGO_SECTOR_MAX];
  uint32_t      sector_cnt;
} algo_dev_t;


struct algo_t;

/* 포맷별 구현이 채우는 표. 호출 규약의 차이가 전부 여기 갇힌다.
   성공 코드 극성(.FLM 은 0, .stldr 은 1)도 각 구현 안에서 흡수한다. */
typedef struct
{
  const char *name;

  // 파일이 이 포맷인지 본다. 확장자가 아니라 심볼로 판단한다.
  bool      (*probe)(elf_t *p_elf);
  // 디바이스 기술자와 심볼을 읽는다. 타깃은 아직 건드리지 않는다.
  bool      (*parse)(struct algo_t *p_algo);
  // 코드를 타깃 RAM 에 올린다 (.FLM 은 재배치, .stldr 은 절대 주소 그대로).
  swd_err_t (*load)(struct algo_t *p_algo, uint32_t ram_base, uint32_t ram_size);

  swd_err_t (*init)(struct algo_t *p_algo, uint32_t fnc);
  swd_err_t (*uninit)(struct algo_t *p_algo, uint32_t fnc);
  swd_err_t (*erase_sector)(struct algo_t *p_algo, uint32_t addr, uint32_t size);
  swd_err_t (*erase_chip)(struct algo_t *p_algo);

  /* 굽기는 시작과 대기를 나눈다. 타깃이 굽는 동안 다음 조각을 다른 버퍼로
     올리기 위한 것이고, AHB-AP 가 코어와 독립이라 성립한다. */
  swd_err_t (*prog_start)(struct algo_t *p_algo, uint32_t addr, uint32_t len, uint32_t buf);
  swd_err_t (*prog_wait)(struct algo_t *p_algo, uint32_t timeout_ms);

  /* 되읽기. 알고리즘이 타깃 RAM 버퍼로 옮겨주면 우리는 그 버퍼를 SWD 로 읽는다.

     내부 플래시는 그냥 주소를 읽으면 되지만 외부 QSPI/NOR 는 memory-mapped
     모드가 아니면 아예 안 읽힌다. 굽고 나서 0x90000000 을 직접 읽으면 0 만
     나온다. NULL 이면 공통 계층이 직접 읽는다. */
  swd_err_t (*read)(struct algo_t *p_algo, uint32_t addr, uint32_t len, uint32_t buf);
} algo_ops_t;


typedef struct algo_t
{
  elf_t            elf;
  algo_kind_t      kind;
  const algo_ops_t *ops;
  algo_dev_t       dev;
  swd_algo_ctx_t   algo;

  uint32_t         fn_init;
  uint32_t         fn_uninit;
  uint32_t         fn_erase_chip;
  uint32_t         fn_erase_sector;
  uint32_t         fn_program;
  uint32_t         fn_read;

  uint32_t         buf_addr;      // 굽기 인자로 넘길 타깃 버퍼
  uint32_t         buf_addr_b;    // 이중 버퍼용 두 번째
  uint32_t         buf_size;      // 한 번에 옮기는 크기
  uint32_t         ram_base;
  uint32_t         ram_end;
  int32_t          delta;         // 재배치량 (.stldr 은 항상 0)
  uint32_t         psize;         // ALGO_PSIZE_*

  bool             is_open;
  bool             is_loaded;
} algo_t;


/* 파일을 열고 포맷을 스스로 판별한다. .FLM 인지 .stldr 인지 확장자로 따지지
   않고 FlashDevice / StorageInfo 심볼로 본다 — 확장자는 사람이 붙여서 틀린다. */
bool        algoOpen(algo_t *p_algo, const char *path);
void        algoClose(algo_t *p_algo);
const char *algoKindStr(algo_t *p_algo);

// 코드를 타깃 RAM 에 올리고 아레나(트램폴린/스택/버퍼)를 잡는다.
swd_err_t   algoLoad(algo_t *p_algo, uint32_t ram_base, uint32_t ram_size);

/* 소거·굽기 병렬도를 정한다. .stldr 만 쓰고 .FLM 은 무시한다. 타깃 VDD 를
   모르면 올리지 않는 게 맞다 — 전압이 모자란 상태에서 큰 단위로 쓰면 플래시가
   조용히 깨진다. 기본값은 가장 안전한 x8 이다. */
void        algoSetPSize(algo_t *p_algo, uint32_t psize);

// 주소가 이 디바이스의 플래시 범위 안인가. 지우기/굽기 전에 반드시 확인한다.
bool        algoIsInRange(algo_t *p_algo, uint32_t addr);
uint32_t    algoSectorBase(algo_t *p_algo, uint32_t addr);
uint32_t    algoSectorSize(algo_t *p_algo, uint32_t addr);

/* 진행 상황 알림 (NULL 이면 생략). */
typedef void (*algo_progress_t)(const char *phase, uint32_t addr, uint32_t done,
                                uint32_t total, void *ctx);

/* 어디서 시간을 쓰는지 나눠서 돌려준다. 소거는 순수 타깃 시간이고 굽기는 우리
   오버헤드가 섞여 있어서, 합쳐 보면 개선할 곳이 안 보인다. */
typedef struct
{
  uint32_t erase_ms;
  uint32_t xfer_ms;       // 조각을 타깃 RAM 버퍼로 옮기는 시간
  uint32_t call_ms;       // 굽기 호출(레지스터 세팅 + 완료 대기)
  uint32_t read_ms;       // SD 카드에서 읽는 시간
  uint32_t page_cnt;
  uint32_t sector_cnt;
} algo_time_t;

/* .bin 은 굽는 주소가 파일에 없어서 인자로 받는다.
   .elf 는 PT_LOAD 의 p_paddr, .hex 는 레코드가 알고 있으므로 인자가 없고
   실제로 쓴 시작 주소를 p_addr 로 돌려준다. */
swd_err_t algoWriteFile(algo_t *p_algo, const char *path, uint32_t addr,
                        algo_progress_t cb, void *ctx, uint32_t *p_written,
                        algo_time_t *p_time);
swd_err_t algoWriteElf(algo_t *p_algo, const char *path,
                       algo_progress_t cb, void *ctx, uint32_t *p_written,
                       algo_time_t *p_time, uint32_t *p_addr);
swd_err_t algoWriteHex(algo_t *p_algo, const char *path,
                       algo_progress_t cb, void *ctx, uint32_t *p_written,
                       algo_time_t *p_time, uint32_t *p_addr);

// 구운 결과를 되읽어 파일과 비교한다. 굽기와 같은 소스를 쓰므로 SD 오류도 걸린다.
swd_err_t algoVerifyFile(algo_t *p_algo, const char *path, uint32_t addr,
                         algo_progress_t cb, void *ctx, uint32_t *p_bad);
swd_err_t algoVerifyElf(algo_t *p_algo, const char *path,
                        algo_progress_t cb, void *ctx, uint32_t *p_bad);
swd_err_t algoVerifyHex(algo_t *p_algo, const char *path,
                        algo_progress_t cb, void *ctx, uint32_t *p_bad);

// 포맷 구현이 타깃 RAM 에 바이트를 옮길 때 쓴다.
bool      algoWriteMem(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx);


#endif


#ifdef __cplusplus
}
#endif

#endif
