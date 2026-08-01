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


#define PROG_LOG_CNT      12
#define PROG_LOG_LEN      56
#define PROG_PROJ_CNT     16


typedef enum
{
  PROG_IDLE = 0,
  PROG_SCANNING,        // 타깃을 찾아 정보를 읽는 중
  PROG_LISTING,         // SD 에서 프로젝트 목록을 훑는 중
  PROG_RUNNING,         // 굽는 중
  PROG_DONE,
  PROG_ERROR,
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
const prog_proj_t   *progTaskGetProj(uint32_t idx);
uint32_t             progTaskGetElapsed(void);

/* 로그는 링버퍼다. seq 가 바뀌었을 때만 다시 그리면 된다. */
uint8_t              progTaskGetLogSeq(void);
uint32_t             progTaskGetLogCnt(void);
const char          *progTaskGetLog(uint32_t idx);   // 0 이 가장 오래된 것


#endif


#ifdef __cplusplus
}
#endif

#endif
