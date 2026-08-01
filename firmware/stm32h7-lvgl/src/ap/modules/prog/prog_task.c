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


#ifdef _USE_HW_SWD


#define PROG_THREAD_STACK   (8 * 1024)


typedef enum
{
  PROG_REQ_NONE = 0,
  PROG_REQ_SCAN,
  PROG_REQ_LIST,
  PROG_REQ_RUN,
} prog_req_t;


static volatile prog_req_t   prog_req;
static volatile prog_state_t prog_state;
static volatile bool         prog_abort;
static volatile uint8_t      prog_pct;
static volatile uint32_t     prog_ms;
static const char           *prog_phase = "";

static char          req_proj[JOB_PROJ_MAX];
static prog_target_t target;
static prog_proj_t   proj_list[PROG_PROJ_CNT];
static uint32_t      proj_cnt;

static char          log_buf[PROG_LOG_CNT][PROG_LOG_LEN];
static uint32_t      log_cnt;
static volatile uint8_t log_seq;


static void progTaskThread(void const *arg);
static void progTaskDoScan(void);
static void progTaskDoList(void);
static void progTaskDoRun(void);
static void progTaskLog(const char *fmt, ...);
static bool progTaskProjCb(const char *proj, const char *name, void *ctx);
static void progTaskProgressCb(const char *phase, uint32_t addr, uint32_t done,
                               uint32_t total, void *ctx);


// ----------------------------------------------------------------- 초기화

bool progTaskInit(void)
{
  prog_state = PROG_IDLE;

  return threadCreate("swd_prog", progTaskThread, NULL,
                      osPriorityBelowNormal, PROG_THREAD_STACK);
}


// ----------------------------------------------------------------- 공개

bool progTaskScan(void)
{
  if (prog_state == PROG_SCANNING || prog_state == PROG_LISTING ||
      prog_state == PROG_RUNNING) return false;

  prog_abort = false;
  prog_req   = PROG_REQ_SCAN;
  return true;
}

bool progTaskList(void)
{
  if (prog_state == PROG_SCANNING || prog_state == PROG_LISTING ||
      prog_state == PROG_RUNNING) return false;

  prog_abort = false;
  prog_req   = PROG_REQ_LIST;
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
  prog_req   = PROG_REQ_RUN;
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
uint8_t      progTaskGetLogSeq(void)  { return log_seq; }
uint32_t     progTaskGetLogCnt(void)  { return log_cnt; }
uint32_t     progTaskGetProjCnt(void) { return proj_cnt; }

const prog_target_t *progTaskGetTarget(void)
{
  return &target;
}

const prog_proj_t *progTaskGetProj(uint32_t idx)
{
  if (idx >= proj_cnt) return NULL;
  return &proj_list[idx];
}

const char *progTaskGetLog(uint32_t idx)
{
  if (idx >= log_cnt) return "";
  return log_buf[idx];
}


// ----------------------------------------------------------------- 내부

static void progTaskThread(void const *arg)
{
  (void)arg;

  while (1)
  {
    prog_req_t req = prog_req;

    if (req != PROG_REQ_NONE)
    {
      prog_req = PROG_REQ_NONE;      // 잡는 즉시 지운다

      switch (req)
      {
        case PROG_REQ_SCAN: progTaskDoScan(); break;
        case PROG_REQ_LIST: progTaskDoList(); break;
        case PROG_REQ_RUN:  progTaskDoRun();  break;
        default: break;
      }
    }
    delay(5);
  }
}

/* 연결된 타깃을 훑는다. 굽지 않고 읽기만 한다.

   순서가 중요하다. DP 를 먼저 깨우고, 코어 디버그가 어느 AP 뒤에 있는지 찾고,
   그 다음에야 CPUID 와 디바이스 판별이 의미가 있다. */
static void progTaskDoScan(void)
{
  uint32_t  idcode = 0;
  uint32_t  t0     = millis();
  swd_err_t err;

  prog_state = PROG_SCANNING;
  prog_phase = "scan";
  prog_pct   = 0;
  log_cnt    = 0;
  log_seq++;

  memset(&target, 0, sizeof(target));

  err = swdConnect(&idcode);
  if (err != SWD_OK)
  {
    progTaskLog("연결 실패 : %s", swdErrStr(err));
    progTaskLog("배선과 타깃 전원을 확인해라");
    prog_ms    = millis() - t0;
    prog_state = PROG_ERROR;
    return;
  }

  target.idcode = idcode;
  target.dp_ver = (idcode >> 12) & 0xF;
  progTaskLog("DPIDR  : 0x%08X  DPv%d", idcode, (int)target.dp_ver);
  prog_pct = 25;

  swdCmInvalidate();
  err = swdCmEnsureAp();
  if (err != SWD_OK)
  {
    progTaskLog("코어 디버그를 못 찾았다");
    prog_ms    = millis() - t0;
    prog_state = PROG_ERROR;
    return;
  }
  target.ap = swdDapGetAp();
  progTaskLog("AP     : %d", (int)target.ap);
  prog_pct = 50;

  swdMemRead32(0xE000ED00, &target.cpuid);
  progTaskLog("CPUID  : 0x%08X", target.cpuid);
  prog_pct = 75;

  err = devDetect(&target.dev, &target.id_read);
  target.dev_found = (err == SWD_OK);

  if (target.dev_found)
  {
    progTaskLog("%s", target.dev.name);
    progTaskLog("ram 0x%08X  %d KB", target.dev.ram, (int)(target.dev.ram_sz / 1024));
    if (target.dev.flash_sz)
    {
      progTaskLog("flash 0x%08X  %d KB", target.dev.flash,
                  (int)(target.dev.flash_sz / 1024));
    }
  }
  else if (err == SWD_ERR_FAULT)
  {
    progTaskLog("0x%08X 에 맞는 항목이 둘 이상", target.id_read);
  }
  else
  {
    progTaskLog("DB 에 없다 (읽은값 0x%08X)", target.id_read);
  }

  target.is_valid = true;
  prog_pct   = 100;
  prog_ms    = millis() - t0;
  prog_state = PROG_DONE;
}

static void progTaskDoList(void)
{
  prog_state = PROG_LISTING;
  prog_phase = "list";
  proj_cnt   = 0;

  jobList(progTaskProjCb, NULL);

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
  log_cnt    = 0;
  log_seq++;

  if (jobLoad(&job, req_proj) == false)
  {
    progTaskLog("fw.txt 를 읽지 못했다");
    prog_ms    = millis() - t0;
    prog_state = PROG_ERROR;
    return;
  }

  progTaskLog("%s", job.name);
  for (uint32_t i = 0; i < job.image_cnt; i++)
  {
    progTaskLog("image  : %s", job.image[i].file);
  }

  err = jobRun(&job, progTaskProgressCb, NULL, true);

  prog_ms = millis() - t0;

  if (err == SWD_OK)
  {
    progTaskLog("완료 : %d.%d 초", (int)(prog_ms / 1000), (int)((prog_ms % 1000) / 100));
    prog_pct   = 100;
    prog_state = PROG_DONE;
  }
  else
  {
    progTaskLog("실패 : %s", swdErrStr(err));
    prog_state = PROG_ERROR;
  }
}

static bool progTaskProjCb(const char *proj, const char *name, void *ctx)
{
  (void)ctx;

  if (proj_cnt >= PROG_PROJ_CNT) return false;

  snprintf(proj_list[proj_cnt].proj, JOB_PROJ_MAX, "%s", proj);
  snprintf(proj_list[proj_cnt].name, JOB_NAME_MAX, "%s", name);
  proj_cnt++;
  return true;
}

/* 잡이 알려오는 진행 상황. 여기서 lv_* 를 부르면 안 된다 — UI 스레드가 아니다. */
static void progTaskProgressCb(const char *phase, uint32_t addr, uint32_t done,
                               uint32_t total, void *ctx)
{
  (void)ctx;

  if (strcmp(phase, "ap") == 0)
  {
    progTaskLog("ap     : %d", (int)addr);
    return;
  }
  if (strcmp(phase, "device") == 0)
  {
    progTaskLog("ram    : 0x%08X", addr);
    return;
  }
  if (strcmp(phase, "clamp") == 0)
  {
    progTaskLog("범위를 %d KB 로 좁힘", (int)(done / 1024));
    return;
  }
  if (strcmp(phase, "dup") == 0)
  {
    progTaskLog("0x%08X 담당이 %d 개", addr, (int)done);
    return;
  }
  if (strcmp(phase, "algo") == 0)
  {
    progTaskLog("algo   : 0x%08X", addr);
    prog_pct = 0;
    return;
  }

  prog_phase = phase;
  if (total > 0) prog_pct = (uint8_t)(done * 100 / total);
}

static void progTaskLog(const char *fmt, ...)
{
  va_list args;

  if (log_cnt >= PROG_LOG_CNT)
  {
    // 한 칸씩 밀어 가장 오래된 것을 버린다
    for (uint32_t i = 0; i + 1 < PROG_LOG_CNT; i++)
    {
      memcpy(log_buf[i], log_buf[i + 1], PROG_LOG_LEN);
    }
    log_cnt = PROG_LOG_CNT - 1;
  }

  va_start(args, fmt);
  vsnprintf(log_buf[log_cnt], PROG_LOG_LEN, fmt, args);
  va_end(args);

  log_cnt++;
  log_seq++;
}


#endif
