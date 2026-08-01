/*
 * prog_task.h
 *
 *  다운로더 워커. UI 와 잡 실행 사이의 유일한 접점이다.
 *
 *  굽기는 10초쯤 걸리고 그 안에서 SD 를 읽고 SWD 를 수천 번 두드린다. UI
 *  스레드에서 부르면 그동안 화면이 통째로 멈춘다. 그래서 스레드를 따로 두고
 *  UI 는 요청만 남긴 뒤 상태를 폴링한다.
 *
 *  규칙이 둘 있다.
 *    - 워커는 lv_* 를 절대 부르지 않는다. LVGL 은 UI 스레드 것이다.
 *    - UI 는 인자를 먼저 쓰고 그 다음에 req 를 세운다. 순서가 바뀌면 워커가
 *      이전 인자로 시작한다.
 */

#ifndef PROG_TASK_H_
#define PROG_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "prog/job/prog_dev.h"
#include "prog/job/prog_job.h"


#ifdef _USE_HW_SWD


#define PROG_STEP_CNT     48      // 단계는 링버퍼가 아니라 누적이다
#define PROG_STEP_LEN     40
#define PROG_PROJ_CNT     16


/* 굽고 나서 타깃을 어떻게 둘지. fw.txt 의 reset 키와 같은 뜻이고 설정 화면에서
   덮어쓴다. 여러 보드에 반복해 굽는 쓰임이라 대개 RUN 이 편하다. */
typedef enum
{
  PROG_RESET_RUN = 0,     // 리셋하고 놓아준다 (보드가 바로 동작)
  PROG_RESET_HALT,        // 리셋 후 정지 상태로 둔다
  PROG_RESET_NONE,        // 아무것도 안 한다
} prog_reset_t;

typedef struct
{
  prog_reset_t reset;
  bool         verify;
  uint8_t      psize;       // ALGO_PSIZE_*
  uint32_t     speed_khz;   // 0 이면 지금 설정을 그대로
  bool         filter;      // 목록에서 이 타깃에 안 맞는 것을 감춘다
} prog_opt_t;

/* 단계 하나. 진행 중인 것과 끝난 것을 같이 보여주려고 상태와 시간을 함께 든다. */
typedef enum
{
  PROG_STEP_RUN = 0,
  PROG_STEP_OK,
  PROG_STEP_FAIL,
} prog_step_state_t;

typedef struct
{
  char              text[PROG_STEP_LEN];
  prog_step_state_t state;
  uint8_t           pct;
  uint32_t          ms;
  uint8_t           depth;    // 0 = 큰 단계, 1 = 그 안의 단계
} prog_step_t;


typedef enum
{
  PROG_IDLE = 0,
  PROG_SCANNING,        // 타깃을 찾아 정보를 읽는 중
  PROG_LISTING,         // SD 에서 프로젝트 목록을 훑는 중
  PROG_RUNNING,         // 굽는 중
  PROG_DONE,
  PROG_ERROR,
  PROG_STATE_CNT,     // UI 가 "아직 못 본 상태" 표시로 쓴다
} prog_state_t;

typedef struct
{
  bool       is_valid;
  uint32_t   idcode;          // DPIDR
  uint32_t   dp_ver;
  uint32_t   id_read;         // DBGMCU 나 TARGETID 에서 읽은 값
  uint32_t   cpuid;
  uint8_t    ap;
  prog_dev_t dev;             // DB 에서 찾은 항목 (is_valid 로 확인)
  bool       dev_found;
} prog_target_t;

typedef struct
{
  char proj[JOB_PROJ_MAX];
  char name[JOB_NAME_MAX];
  /* fw.txt 의 device. 비어 있으면 자동 판별에 맡긴다는 뜻이라 미리 호환을
     확인할 수 없다 — 목록에서 그렇게 표시한다. */
  char device[DEV_NAME_MAX];
} prog_proj_t;


bool                 progTaskInit(void);

/* UI -> 워커. 이미 무언가 돌고 있으면 false 를 돌려준다. */
bool                 progTaskScan(void);
bool                 progTaskList(void);
bool                 progTaskRun(const char *project);
void                 progTaskAbort(void);

/* 워커 -> UI. 전부 읽기 전용이고 폴링해서 본다. */
prog_state_t         progTaskGetState(void);
uint8_t              progTaskGetPercent(void);
const char          *progTaskGetPhase(void);
const prog_target_t *progTaskGetTarget(void);
uint32_t             progTaskGetProjCnt(void);
// 목록이 다시 만들어질 때마다 바뀐다. 열려 있는 목록 화면을 갱신하는 신호다.
uint8_t              progTaskGetProjSeq(void);
const prog_proj_t   *progTaskGetProj(uint32_t idx);
uint32_t             progTaskGetElapsed(void);

/* 단계 목록. seq 가 바뀌었을 때만 다시 그리면 된다.
   지나간 단계를 지우지 않는다 — 실패했을 때 어디까지 갔었는지가 가장 알고 싶다. */
uint8_t              progTaskGetStepSeq(void);
uint32_t             progTaskGetStepCnt(void);
const prog_step_t   *progTaskGetStep(uint32_t idx);

/* 설정. 바꾸면 NVS 에 저장한다. */
const prog_opt_t    *progTaskGetOpt(void);
void                 progTaskSetOpt(const prog_opt_t *p_opt);

/* 마지막으로 고른 프로젝트. 전원을 껐다 켜도 남는다 — 같은 펌웨어를 여러 보드에
   반복해 굽는 쓰임이라 매번 고르게 하면 안 된다. */
const char          *progTaskGetProject(void);
void                 progTaskSetProject(const char *project);

// 이번 전원 인가 후 성공한 횟수. 반복 작업에서 세는 게 실제로 쓸모 있다.
uint32_t             progTaskGetOkCount(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
