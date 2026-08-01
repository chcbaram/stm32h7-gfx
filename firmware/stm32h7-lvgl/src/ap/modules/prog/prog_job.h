/*
 * prog_job.h
 *
 *  펌웨어 잡. /prog/fw/<프로젝트>/fw.txt 하나가 잡 하나다.
 *
 *  최소 형태는 두 줄이다.
 *
 *      name  = F411 Motor Controller v1.4
 *      image = app.elf
 *
 *  device 를 안 적으면 타깃에서 읽어 판별하고, 알고리즘은 DB 의 것을 쓰고,
 *  주소는 .elf/.hex 가 스스로 안다. 유도할 수 있는 건 적지 않는다.
 *
 *  필요할 때만 덧붙인다.
 *
 *      device = STM32F411xC/E      자동 판별을 끄고 강제 지정
 *      algo   = /prog/loaders/st/0x431.stldr        내부 플래시 알고리즘
 *      loader = /prog/loaders/ext/MX25L512G_xx.stldr 외부 메모리 로더
 *      psize  = 2                  소거/굽기 병렬도 (.stldr 전용)
 *      speed  = 3500               SWD kHz
 *      image  = app.elf
 *      image  = res.bin @ 0x90000000
 *
 *  이미지가 어느 알고리즘으로 갈지는 주소로 정해진다. 알고리즘마다 자기가
 *  담당하는 주소 범위를 알고 있으므로 따로 적을 문법이 필요 없다.
 *
 *  알고리즘 둘을 동시에 타깃 RAM 에 올리지는 않는다. .stldr 은 절대 주소로
 *  링크되어 있어 서로 겹친다. 그래서 알고리즘 하나를 올려 그 담당 이미지를
 *  전부 처리하고, 리셋한 뒤 다음 알고리즘을 올린다.
 */

#ifndef PROG_JOB_H_
#define PROG_JOB_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "prog/prog_algo.h"
#include "prog/prog_dev.h"


#ifdef _USE_HW_SWD


#define JOB_IMAGE_MAX   8
#define JOB_PATH_MAX    96
#define JOB_NAME_MAX    48
#define JOB_PROJ_MAX    32


typedef struct
{
  char     file[JOB_PATH_MAX];
  uint32_t addr;
  bool     has_addr;        // fw.txt 에 @ 주소가 적혀 있었나
} job_image_t;

typedef struct
{
  char        proj[JOB_PROJ_MAX];     // 프로젝트 폴더 이름
  char        dir[JOB_PATH_MAX];      // fw.txt 가 있는 폴더 (상대 경로의 기준)
  char        name[JOB_NAME_MAX];
  char        device[DEV_NAME_MAX];
  char        algo[JOB_PATH_MAX];     // 내부. 비어 있으면 DB 의 algo
  char        loader[JOB_PATH_MAX];   // 외부. 비어 있으면 안 쓴다
  uint32_t    psize;
  uint32_t    speed_khz;
  bool        has_psize;
  bool        has_speed;

  job_image_t image[JOB_IMAGE_MAX];
  uint32_t    image_cnt;
} job_t;


// /prog/fw/<프로젝트>/fw.txt 를 읽는다. 타깃은 건드리지 않는다.
bool      jobLoad(job_t *p_job, const char *project);

// /prog/fw 아래에서 fw.txt 를 가진 폴더를 훑는다. cb 가 false 면 멈춘다.
uint32_t  jobList(bool (*cb)(const char *proj, const char *name, void *ctx), void *ctx);

/* 잡을 실행한다. 연결·판별·리셋·소거·굽기·검증까지 전부 여기서 한다.
   진행 상황은 cb 로 알린다 (NULL 이면 생략). */
swd_err_t jobRun(job_t *p_job, algo_progress_t cb, void *ctx, bool do_verify);

// 상대 경로를 fw.txt 기준으로 푼다. '/' 로 시작하면 절대 경로로 본다.
void      jobPath(const job_t *p_job, const char *in, char *p_out, uint32_t max);


#endif


#ifdef __cplusplus
}
#endif

#endif
