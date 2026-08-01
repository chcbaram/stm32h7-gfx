/*
 * prog_task.c
 *
 *  다운로더 워커
 *
 *  UI 는 요청만 남기고 상태를 폴링한다. 워커는 lv_* 를 부르지 않는다.
 */

#include "prog/prog_task.h"
#include "prog/algo/prog_algo.h"
#include "swd.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"
#include "nvs.h"


#ifdef _USE_HW_SWD


#define PROG_THREAD_STACK   (8 * 1024)


/* 요청은 비트마스크다.

   단일 변수로 두었더니 UI 가 목록과 스캔을 연달아 요청할 때 뒤엣것이 앞엣것을
   덮어썼다. 워커는 5ms 마다 보는데 그 사이에 둘이 들어오면 하나가 사라진다.
   실제로 앱에 들어오자마자 목록이 통째로 비어 있었다. */
#define PROG_REQ_SCAN   (1 << 0)
#define PROG_REQ_LIST   (1 << 1)
#define PROG_REQ_RUN    (1 << 2)


static volatile uint32_t     prog_req;
static volatile prog_state_t prog_state;
static volatile bool         prog_abort;
static volatile uint8_t      prog_pct;
static volatile uint32_t     prog_ms;
static const char           *prog_phase = "";

static char          req_proj[JOB_PROJ_MAX];
static prog_target_t target;
/* 목록은 두 벌이다. 만드는 동안 보여주는 쪽을 건드리지 않으려는 것이다.
   한 벌로 두고 시작할 때 개수를 0 으로 만들면, 화면이 빈 목록을 한 번 그렸다가
   1초쯤 뒤 다시 채워져 깜빡이는 것처럼 보인다. */
static prog_proj_t   proj_list[PROG_PROJ_CNT];    // UI 가 읽는다
static prog_proj_t   proj_build[PROG_PROJ_CNT];   // 워커가 만든다
static uint32_t      proj_cnt;
static uint32_t      proj_build_cnt;
static volatile uint8_t proj_seq;

static prog_step_t   step[PROG_STEP_CNT];
static uint32_t      step_cnt;
static volatile uint8_t step_seq;
static uint32_t      step_t0;          // 지금 진행 중인 단계의 시작 시각

static prog_opt_t    opt = { PROG_RESET_RUN, true, ALGO_PSIZE_8, 0 };
static char          sel_project[JOB_PROJ_MAX];
static uint32_t      ok_count;

#define PROG_NVS_OPT   "prog.opt"
#define PROG_NVS_PROJ  "prog.proj"


static void progTaskThread(void const *arg);
static void progTaskDoScan(void);
static void progTaskDoList(void);
static void progTaskDoRun(void);
static void progTaskStep(uint8_t depth, const char *fmt, ...);
static void progTaskStepEnd(prog_step_state_t st);
static bool progTaskProjCb(const char *proj, const char *name, void *ctx);
static void progTaskProgressCb(const char *phase, uint32_t addr, uint32_t done,
                               uint32_t total, void *ctx);


// ----------------------------------------------------------------- 초기화

bool progTaskInit(void)
{
  prog_state = PROG_IDLE;

  /* 설정과 마지막 프로젝트를 되살린다. 같은 펌웨어를 여러 보드에 반복해 굽는
     쓰임이라 전원을 껐다 켤 때마다 다시 고르게 하면 안 된다. */
  nvsGet(PROG_NVS_OPT,  &opt,         sizeof(opt));
  nvsGet(PROG_NVS_PROJ, sel_project, sizeof(sel_project));
  sel_project[sizeof(sel_project) - 1] = 0;
  if (opt.psize > ALGO_PSIZE_64) opt.psize = ALGO_PSIZE_8;
  if (opt.reset > PROG_RESET_NONE) opt.reset = PROG_RESET_RUN;

  return threadCreate("swd_prog", progTaskThread, NULL,
                      osPriorityBelowNormal, PROG_THREAD_STACK);
}


// ----------------------------------------------------------------- 공개

bool progTaskScan(void)
{
  if (prog_state == PROG_SCANNING || prog_state == PROG_LISTING ||
      prog_state == PROG_RUNNING) return false;

  prog_abort = false;
  prog_req  |= PROG_REQ_SCAN;
  return true;
}

bool progTaskList(void)
{
  if (prog_state == PROG_SCANNING || prog_state == PROG_LISTING ||
      prog_state == PROG_RUNNING) return false;

  prog_abort = false;
  prog_req  |= PROG_REQ_LIST;
  return true;
}

bool progTaskRun(const char *project)
{
  if (project == NULL) return false;
  if (prog_state == PROG_SCANNING || prog_state == PROG_LISTING ||
      prog_state == PROG_RUNNING) return false;

  // 인자를 먼저 쓰고 그 다음에 요청을 세운다. 순서가 바뀌면 워커가 이전 값을 본다.
  snprintf(req_proj, sizeof(req_proj), "%s", project);
  __DMB();

  prog_abort = false;
  prog_req  |= PROG_REQ_RUN;
  return true;
}

void progTaskAbort(void)
{
  prog_abort = true;
}

prog_state_t progTaskGetState(void)   { return prog_state; }
uint8_t      progTaskGetPercent(void) { return prog_pct; }
const char  *progTaskGetPhase(void)   { return prog_phase; }
uint32_t     progTaskGetElapsed(void) { return prog_ms; }
uint8_t      progTaskGetStepSeq(void) { return step_seq; }
uint32_t     progTaskGetStepCnt(void) { return step_cnt; }
uint32_t     progTaskGetOkCount(void) { return ok_count; }
const char  *progTaskGetProject(void) { return sel_project; }

const prog_step_t *progTaskGetStep(uint32_t idx)
{
  if (idx >= step_cnt) return NULL;
  return &step[idx];
}

const prog_opt_t *progTaskGetOpt(void)
{
  return &opt;
}

void progTaskSetOpt(const prog_opt_t *p_opt)
{
  if (p_opt == NULL) return;

  opt = *p_opt;
  nvsSet(PROG_NVS_OPT, &opt, sizeof(opt));
}

void progTaskSetProject(const char *project)
{
  if (project == NULL) return;

  snprintf(sel_project, sizeof(sel_project), "%s", project);
  nvsSet(PROG_NVS_PROJ, sel_project, sizeof(sel_project));
}
uint32_t     progTaskGetProjCnt(void) { return proj_cnt; }
uint8_t      progTaskGetProjSeq(void) { return proj_seq; }

const prog_target_t *progTaskGetTarget(void)
{
  return &target;
}

const prog_proj_t *progTaskGetProj(uint32_t idx)
{
  if (idx >= proj_cnt) return NULL;
  return &proj_list[idx];
}

// ----------------------------------------------------------------- 내부

static void progTaskThread(void const *arg)
{
  (void)arg;

  while (1)
  {
    uint32_t req = prog_req;

    /* 하나씩 처리한다. 잡은 비트만 지워서 같이 들어온 다른 요청이 살아남는다.
       목록 -> 스캔 -> 굽기 순인데, 굽기가 가장 오래 걸리므로 나중이다. */
    if (req & PROG_REQ_LIST)
    {
      prog_req &= ~PROG_REQ_LIST;
      progTaskDoList();
    }
    else if (req & PROG_REQ_SCAN)
    {
      prog_req &= ~PROG_REQ_SCAN;
      progTaskDoScan();
    }
    else if (req & PROG_REQ_RUN)
    {
      prog_req &= ~PROG_REQ_RUN;
      progTaskDoRun();
    }
    delay(5);
  }
}

/* 연결된 타깃을 훑는다. 굽지 않고 읽기만 한다.

   순서가 중요하다. DP 를 먼저 깨우고, 코어 디버그가 어느 AP 뒤에 있는지 찾고,
   그 다음에야 CPUID 와 디바이스 판별이 의미가 있다.

   결과는 임시 구조체에 모았다가 마지막에 한 번에 옮긴다. 시작할 때 target 을
   지우면 화면이 "-" 로 돌아갔다가 1초쯤 뒤 다시 채워져 깜빡이는 것처럼 보인다.
   이전에 알아낸 것은 새 답이 나올 때까지 그대로 두는 게 맞다. */
static void progTaskDoScan(void)
{
  prog_target_t tmp;
  uint32_t      idcode = 0;
  uint32_t      t0     = millis();
  swd_err_t     err;

  prog_state = PROG_SCANNING;
  prog_phase = "scan";
  prog_pct   = 0;
  step_cnt   = 0;
  step_seq++;

  memset(&tmp, 0, sizeof(tmp));

  err = swdConnect(&idcode);
  if (err != SWD_OK)
  {
    progTaskStep(0, "연결 실패 : %s", swdErrStr(err));
    progTaskStepEnd(PROG_STEP_FAIL);
    progTaskStep(0, "배선과 타깃 전원을 확인해라");
    progTaskStepEnd(PROG_STEP_FAIL);

    target     = tmp;              // 연결이 끊긴 건 바로 알려야 한다
    prog_ms    = millis() - t0;
    prog_state = PROG_ERROR;
    return;
  }

  tmp.idcode = idcode;
  tmp.dp_ver = (idcode >> 12) & 0xF;
  progTaskStep(0, "DPIDR  : 0x%08X  DPv%d", idcode, (int)tmp.dp_ver);
  progTaskStepEnd(PROG_STEP_OK);
  prog_pct = 25;

  swdCmInvalidate();
  err = swdCmEnsureAp();
  if (err != SWD_OK)
  {
    progTaskStep(0, "코어 디버그를 못 찾았다");
    progTaskStepEnd(PROG_STEP_FAIL);

    target     = tmp;
    prog_ms    = millis() - t0;
    prog_state = PROG_ERROR;
    return;
  }
  tmp.ap = swdDapGetAp();
  progTaskStep(0, "AP     : %d", (int)tmp.ap);
  progTaskStepEnd(PROG_STEP_OK);
  prog_pct = 50;

  swdMemRead32(0xE000ED00, &tmp.cpuid);
  progTaskStep(0, "CPUID  : 0x%08X", tmp.cpuid);
  progTaskStepEnd(PROG_STEP_OK);
  prog_pct = 75;

  err = devDetect(&tmp.dev, &tmp.id_read);
  tmp.dev_found = (err == SWD_OK);

  if (tmp.dev_found)
  {
    progTaskStep(0, "%s", tmp.dev.name);
    progTaskStepEnd(PROG_STEP_OK);
    progTaskStep(0, "ram 0x%08X  %d KB", tmp.dev.ram, (int)(tmp.dev.ram_sz / 1024));
    progTaskStepEnd(PROG_STEP_OK);
    if (tmp.dev.flash_sz)
    {
      progTaskStep(0, "flash 0x%08X  %d KB", tmp.dev.flash,
                   (int)(tmp.dev.flash_sz / 1024));
      progTaskStepEnd(PROG_STEP_OK);
    }
  }
  else if (err == SWD_ERR_FAULT)
  {
    progTaskStep(0, "0x%08X 에 맞는 항목이 둘 이상", tmp.id_read);
    progTaskStepEnd(PROG_STEP_FAIL);
  }
  else
  {
    progTaskStep(0, "DB 에 없다 (읽은값 0x%08X)", tmp.id_read);
    progTaskStepEnd(PROG_STEP_FAIL);
  }

  tmp.is_valid = true;
  target       = tmp;              // 다 끝난 뒤에 한 번에 바꾼다

  prog_pct   = 100;
  prog_ms    = millis() - t0;
  prog_state = PROG_DONE;
}

static void progTaskDoList(void)
{
  prog_state     = PROG_LISTING;
  prog_phase     = "list";
  proj_build_cnt = 0;

  jobList(progTaskProjCb, NULL);

  // 다 만든 뒤에 한 번에 바꾼다
  memcpy(proj_list, proj_build, sizeof(proj_list));
  proj_cnt = proj_build_cnt;

  proj_seq++;
  prog_state = PROG_DONE;
}

static void progTaskDoRun(void)
{
  static job_t job;
  uint32_t     t0 = millis();
  swd_err_t    err;

  prog_state = PROG_RUNNING;
  prog_phase = "load";
  prog_pct   = 0;
  step_cnt   = 0;
  step_seq++;

  progTaskStep(0, "%s", req_proj);

  if (jobLoad(&job, req_proj) == false)
  {
    progTaskStepEnd(PROG_STEP_FAIL);
    progTaskStep(0, "fw.txt 를 읽지 못했다");
    progTaskStepEnd(PROG_STEP_FAIL);
    prog_ms    = millis() - t0;
    prog_state = PROG_ERROR;
    return;
  }

  /* 설정이 fw.txt 를 덮어쓴다. 화면에서 고른 값이 파일보다 우선이어야
     "지금 이 보드는 리셋하지 말고" 같은 즉석 판단이 가능하다. */
  job.has_psize = true;
  job.psize     = opt.psize;
  if (opt.speed_khz > 0)
  {
    job.has_speed  = true;
    job.speed_khz  = opt.speed_khz;
  }

  err = jobRun(&job, progTaskProgressCb, NULL, opt.verify);
  progTaskStepEnd((err == SWD_OK) ? PROG_STEP_OK : PROG_STEP_FAIL);

  // 굽고 나서 타깃을 어떻게 둘지
  if (err == SWD_OK && opt.reset != PROG_RESET_NONE)
  {
    progTaskStep(0, (opt.reset == PROG_RESET_RUN) ? "리셋 후 실행" : "리셋 후 정지");
    if (swdCmSysReset() == SWD_OK && opt.reset == PROG_RESET_RUN)
    {
      swdCmDetach();
    }
    progTaskStepEnd(PROG_STEP_OK);
  }

  prog_ms = millis() - t0;

  if (err == SWD_OK)
  {
    ok_count++;
    prog_pct   = 100;
    prog_state = PROG_DONE;
  }
  else
  {
    prog_state = PROG_ERROR;
  }
}

/* 만드는 쪽 버퍼에만 담는다. 보여주는 쪽은 다 만든 뒤에 한 번에 바뀐다. */
static bool progTaskProjCb(const char *proj, const char *name, void *ctx)
{
  (void)ctx;

  if (proj_build_cnt >= PROG_PROJ_CNT) return false;

  snprintf(proj_build[proj_build_cnt].proj, JOB_PROJ_MAX, "%s", proj);
  snprintf(proj_build[proj_build_cnt].name, JOB_NAME_MAX, "%s", name);
  proj_build_cnt++;
  return true;
}

/* 잡이 알려오는 진행 상황. 여기서 lv_* 를 부르면 안 된다 — UI 스레드가 아니다. */
static void progTaskProgressCb(const char *phase, uint32_t addr, uint32_t done,
                               uint32_t total, void *ctx)
{
  (void)ctx;

  if (strcmp(phase, "ap") == 0)
  {
    progTaskStepEnd(PROG_STEP_OK);
    progTaskStep(0, "AP %d 선택", (int)addr);
    progTaskStepEnd(PROG_STEP_OK);
    return;
  }
  if (strcmp(phase, "device") == 0)  return;
  if (strcmp(phase, "clamp") == 0)
  {
    progTaskStep(0, "범위를 %d KB 로 좁힘", (int)(done / 1024));
    progTaskStepEnd(PROG_STEP_OK);
    return;
  }
  if (strcmp(phase, "dup") == 0)
  {
    progTaskStep(0, "0x%08X 담당이 %d 개", addr, (int)done);
    progTaskStepEnd(PROG_STEP_FAIL);
    return;
  }
  if (strcmp(phase, "algo") == 0)
  {
    progTaskStepEnd(PROG_STEP_OK);
    progTaskStep(0, "0x%08X", addr);
    prog_pct = 0;
    return;
  }

  /* erase / program / verify. 단계가 바뀔 때만 새로 만들고 그 안에서는
     퍼센트만 갱신한다 — 페이지마다 줄을 만들면 목록이 순식간에 넘친다. */
  if (phase != prog_phase)
  {
    progTaskStepEnd(PROG_STEP_OK);
    progTaskStep(1, "%s", phase);
    prog_phase = phase;
  }
  if (total > 0)
  {
    prog_pct = (uint8_t)(done * 100 / total);
    if (step_cnt > 0)
    {
      step[step_cnt - 1].pct = prog_pct;
      step[step_cnt - 1].ms  = millis() - step_t0;
      step_seq++;
    }
  }
}

/* 단계를 하나 시작한다. 지나간 단계는 지우지 않는다 — 실패했을 때 어디까지
   갔었는지가 가장 알고 싶은 정보다. 가득 차면 더 담지 않는다. */
static void progTaskStep(uint8_t depth, const char *fmt, ...)
{
  va_list args;

  if (step_cnt >= PROG_STEP_CNT) return;

  memset(&step[step_cnt], 0, sizeof(prog_step_t));

  va_start(args, fmt);
  vsnprintf(step[step_cnt].text, PROG_STEP_LEN, fmt, args);
  va_end(args);

  step[step_cnt].state = PROG_STEP_RUN;
  step[step_cnt].depth = depth;
  step_t0 = millis();
  step_cnt++;
  step_seq++;
}

// 진행 중이던 단계를 마감한다. 없으면 아무 일도 하지 않는다.
static void progTaskStepEnd(prog_step_state_t st)
{
  if (step_cnt == 0) return;
  if (step[step_cnt - 1].state != PROG_STEP_RUN) return;

  step[step_cnt - 1].state = st;
  step[step_cnt - 1].ms    = millis() - step_t0;
  step_seq++;
}

#endif
