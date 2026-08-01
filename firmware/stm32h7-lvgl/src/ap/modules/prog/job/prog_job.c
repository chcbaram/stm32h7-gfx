/*
 * prog_job.c
 *
 *  펌웨어 잡 실행
 *
 *  흐름
 *    1. fw.txt 를 읽는다
 *    2. 연결하고 디바이스를 정한다 (명시 아니면 자동 판별)
 *    3. 이미지마다 시작 주소를 알아낸다 (.elf/.hex 는 파일이 안다)
 *    4. 알고리즘마다 담당 이미지를 모은다 (주소 범위로 가른다)
 *    5. 알고리즘 하나씩 - 리셋 후 로드하고 담당 이미지를 굽는다
 *    6. 검증하고 타깃을 놓아준다
 *
 *  알고리즘을 하나씩 올리는 건 .stldr 이 절대 주소로 링크되어 있어서다.
 *  내부용과 외부용을 같이 올리면 서로 덮어쓴다.
 */

#include "prog/job/prog_job.h"
#include "prog/job/prog_cfg.h"
#include "prog/image/prog_hex.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"


#ifdef _USE_HW_SWD


#define JOB_ALGO_MAX    (1 + JOB_LOADER_MAX)   // 내부 하나 + 외부 여럿


/* 알고리즘 파일은 담당 범위만 읽고 바로 닫는다.

   FatFs 가 _FS_LOCK = 2 라 파일을 두 개까지만 동시에 열 수 있다. 알고리즘 둘을
   열어둔 채로 이미지를 열면 세 번째가 거부되고, 그러면 이미지 주소를 못 읽어
   조용히 기본값으로 떨어진다 — 실제로 그렇게 외부 QSPI 용 이미지가 내부
   플래시 알고리즘에 배정됐다. 열어둘 이유도 없다. */
typedef struct
{
  algo_t   algo;
  char     path[JOB_PATH_MAX];
  uint32_t dev_adr;
  uint32_t sz_dev;              // DB 로 좁힌 값
  uint32_t img[JOB_IMAGE_MAX];  // 담당 이미지 인덱스
  uint32_t img_cnt;
  bool     is_valid;
} job_algo_t;


static bool      jobLineCb(const char *sec, const char *key, const char *val, void *ctx);
static bool      jobImageAddr(const job_t *p_job, const job_image_t *p_img, uint32_t *p_addr);
static swd_err_t jobRunAlgo(job_t *p_job, job_algo_t *p_ja, const prog_dev_t *p_dev,
                            algo_progress_t cb, void *ctx, bool do_verify,
                            uint32_t *p_bad);


// ----------------------------------------------------------------- 공개

bool jobLoad(job_t *p_job, const char *project)
{
  char path[JOB_PATH_MAX + JOB_PROJ_MAX + 16];

  if (p_job == NULL || project == NULL) return false;

  memset(p_job, 0, sizeof(job_t));
  p_job->ap = 0xFF;
  snprintf(p_job->proj, sizeof(p_job->proj), "%s", project);
  snprintf(p_job->dir,  sizeof(p_job->dir),  "%s/%s", HW_SWD_SD_FW, project);
  snprintf(path, sizeof(path), "%s/fw.txt", p_job->dir);

  if (cfgParse(path, jobLineCb, p_job) == false) return false;

  if (p_job->name[0] == 0) snprintf(p_job->name, sizeof(p_job->name), "%s", project);

  return (p_job->image_cnt > 0);
}

uint32_t jobList(bool (*cb)(const char *proj, const char *name, void *ctx), void *ctx)
{
  DIR      dir;
  FILINFO  fno;
  uint32_t cnt = 0;

  if (f_opendir(&dir, HW_SWD_SD_FW) != FR_OK) return 0;

  while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0)
  {
    char    path[288];
    FILINFO st;

    if ((fno.fattrib & AM_DIR) == 0) continue;

    snprintf(path, sizeof(path), "%s/%s/fw.txt", HW_SWD_SD_FW, fno.fname);
    if (f_stat(path, &st) != FR_OK) continue;

    cnt++;
    if (cb != NULL)
    {
      job_t job;
      const char *nm = fno.fname;

      if (jobLoad(&job, fno.fname)) nm = job.name;
      if (cb(fno.fname, nm, ctx) == false) break;
    }
  }

  f_closedir(&dir);
  return cnt;
}

swd_err_t jobRun(job_t *p_job, algo_progress_t cb, void *ctx, bool do_verify)
{
  static job_algo_t ja[JOB_ALGO_MAX];
  prog_dev_t dev;
  swd_err_t  err;
  uint32_t   id = 0;
  uint32_t   n_algo = 0;
  uint32_t   bad = 0;

  if (p_job == NULL || p_job->image_cnt == 0) return SWD_ERR_PROTOCOL;

  memset(ja, 0, sizeof(ja));
  memset(&dev, 0, sizeof(dev));

  // 1) 연결
  if (p_job->has_speed) swdSetSpeed(p_job->speed_khz);

  err = swdConnect(NULL);
  if (err != SWD_OK) return err;

  /* 코어 디버그가 어느 AP 뒤에 있는지 먼저 정한다. 이게 틀리면 디바이스 판별도
     알고리즘 로드도 전부 엉뚱한 버스로 간다. */
  swdCmInvalidate();
  if (p_job->ap != 0xFF)
  {
    swdCmSetAp(p_job->ap);
  }
  else
  {
    err = swdCmEnsureAp();
    if (err != SWD_OK) return err;
  }
  if (cb) cb("ap", swdDapGetAp(), 0, 0, ctx);

  /* 2) 디바이스.

     fw.txt 가 device 를 적었더라도 타깃을 직접 읽어 대조한다. 지우기 직전에
     신원을 확인하는 이 한 번이, 엉뚱한 보드를 지우는 걸 막는 유일한 장치다.

     실제로 F411 을 물린 채 H7RS 용 잡을 돌렸다. 대조가 없으면 H7RS 알고리즘을
     F411 RAM 에 올려 굽기 시작한다. */
  {
    prog_dev_t found;

    err = devDetect(&found, &id);

    if (p_job->device[0] != 0)
    {
      if (devFind(p_job->device, &dev) == false) return SWD_ERR_PROTOCOL;

      if (err == SWD_OK && strcmp(found.name, dev.name) != 0)
      {
        if (cb) cb("mismatch", 0, 0, 0, ctx);
        return SWD_ERR_MISMATCH;
      }
    }
    else
    {
      if (err != SWD_OK) return err;
      dev = found;
    }
  }
  if (p_job->ap == 0xFF && dev.ap != 0xFF) swdCmSetAp(dev.ap);
  if (cb) cb("device", dev.ram, 0, 0, ctx);

  // 3) 알고리즘 목록을 만든다 (열기만 한다. 타깃은 아직 안 건드린다)
  {
    const char *paths[JOB_ALGO_MAX];
    uint32_t    n_path = 0;

    /* 내부를 먼저 담는다. 주소가 없는 .bin 이 기본으로 갈 곳이라 순서가 있다. */
    paths[n_path++] = (p_job->algo[0] != 0) ? p_job->algo : dev.algo;
    for (uint32_t i = 0; i < p_job->loader_cnt; i++) paths[n_path++] = p_job->loader[i];

    for (uint32_t i = 0; i < n_path; i++)
    {
      if (paths[i] == NULL || paths[i][0] == 0) continue;

      jobPath(p_job, paths[i], ja[n_algo].path, sizeof(ja[n_algo].path));
      if (algoOpen(&ja[n_algo].algo, ja[n_algo].path) == false) return SWD_ERR_PROTOCOL;

      ja[n_algo].dev_adr = ja[n_algo].algo.dev.dev_adr;
      ja[n_algo].sz_dev  = ja[n_algo].algo.dev.sz_dev;

      /* 알고리즘이 자기 크기를 틀리게 적은 경우가 있어서 DB 값으로 좁힌다.
         GigaDevice 팩의 1MB/2MB .FLM 이 둘 다 3840KB 라고 한다. 넓게 잡힌
         범위는 지우기 방어를 그대로 무력화한다. */
      if (dev.flash_sz != 0 &&
          ja[n_algo].dev_adr == dev.flash &&
          ja[n_algo].sz_dev  >  dev.flash_sz)
      {
        ja[n_algo].sz_dev = dev.flash_sz;
        if (cb) cb("clamp", dev.flash, dev.flash_sz, 0, ctx);
      }

      algoClose(&ja[n_algo].algo);      // 범위만 알면 된다. 열어두면 이미지를 못 연다.
      ja[n_algo].is_valid = true;
      n_algo++;
    }
  }
  if (n_algo == 0) return SWD_ERR_PROTOCOL;

  // 4) 이미지를 주소로 갈라 담는다
  for (uint32_t i = 0; i < p_job->image_cnt; i++)
  {
    uint32_t addr = 0;
    bool     put  = false;

    if (jobImageAddr(p_job, &p_job->image[i], &addr) == false)
    {
      addr = ja[0].algo.dev.dev_adr;      // .bin 인데 주소가 없으면 첫 알고리즘 기준
    }
    p_job->image[i].addr = addr;

    {
      uint32_t hit = 0;
      uint32_t sel = 0;

      for (uint32_t k = 0; k < n_algo; k++)
      {
        if (addr < ja[k].dev_adr || addr >= ja[k].dev_adr + ja[k].sz_dev) continue;
        if (hit == 0) sel = k;
        hit++;
      }

      /* 담당이 둘 이상이면 조용히 첫 번째를 쓰면 안 된다. 로더 두 개가 같은
         주소를 주장하는 건 설정이 잘못된 것이고, 잘못된 쪽으로 구우면 지운
         뒤에야 알게 된다. */
      if (hit == 1 && ja[sel].img_cnt < JOB_IMAGE_MAX)
      {
        ja[sel].img[ja[sel].img_cnt++] = i;
        put = true;
      }
      else if (hit > 1 && cb)
      {
        cb("dup", addr, hit, 0, ctx);
      }
    }
    if (put == false)
    {
      return SWD_ERR_PROTOCOL;            // 담당이 없거나 둘 이상이다
    }
  }

  // 5) 알고리즘 하나씩
  for (uint32_t k = 0; k < n_algo; k++)
  {
    if (ja[k].img_cnt == 0) continue;

    err = jobRunAlgo(p_job, &ja[k], &dev, cb, ctx, do_verify, &bad);
    if (err != SWD_OK) return err;
  }

  return (bad == 0) ? SWD_OK : SWD_ERR_FAULT;
}

void jobPath(const job_t *p_job, const char *in, char *p_out, uint32_t max)
{
  if (in == NULL || in[0] == 0)
  {
    if (max) p_out[0] = 0;
    return;
  }

  if (in[0] == '/') snprintf(p_out, max, "%s", in);
  else              snprintf(p_out, max, "%s/%s", p_job->dir, in);
}


// ----------------------------------------------------------------- 내부

static bool jobLineCb(const char *sec, const char *key, const char *val, void *ctx)
{
  job_t *p_job = (job_t *)ctx;

  (void)sec;                              // fw.txt 는 섹션을 쓰지 않는다

  if (strcmp(key, "name") == 0)
  {
    snprintf(p_job->name, sizeof(p_job->name), "%s", val);
  }
  else if (strcmp(key, "device") == 0)
  {
    snprintf(p_job->device, sizeof(p_job->device), "%s", val);
  }
  else if (strcmp(key, "algo") == 0)
  {
    snprintf(p_job->algo, sizeof(p_job->algo), "%s", val);
  }
  else if (strcmp(key, "loader") == 0)
  {
    if (p_job->loader_cnt < JOB_LOADER_MAX)
    {
      snprintf(p_job->loader[p_job->loader_cnt], JOB_PATH_MAX, "%s", val);
      p_job->loader_cnt++;
    }
  }
  else if (strcmp(key, "ap") == 0)
  {
    p_job->ap = (uint8_t)cfgNum(val);
  }
  else if (strcmp(key, "psize") == 0)
  {
    p_job->psize     = cfgNum(val);
    p_job->has_psize = true;
  }
  else if (strcmp(key, "speed") == 0)
  {
    p_job->speed_khz = cfgNum(val);
    p_job->has_speed = true;
  }
  else if (strcmp(key, "image") == 0)
  {
    /* image = <파일> [@ <주소>]
       image 키는 여러 번 나올 수 있다. 배열을 쓰는 대신 반복을 허용하는 게
       손으로 적기에 훨씬 낫다. */
    job_image_t *p_img;
    char        *at;
    char         buf[CFG_VAL_MAX];

    if (p_job->image_cnt >= JOB_IMAGE_MAX) return true;

    p_img = &p_job->image[p_job->image_cnt];
    snprintf(buf, sizeof(buf), "%s", val);

    at = strchr(buf, '@');
    if (at != NULL)
    {
      *at++ = 0;
      p_img->addr     = cfgNum(at);
      p_img->has_addr = true;
    }

    // 파일명 뒤 공백 정리
    {
      uint32_t n = (uint32_t)strlen(buf);

      while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t')) buf[--n] = 0;
    }
    if (buf[0] == 0) return true;

    snprintf(p_img->file, sizeof(p_img->file), "%s", buf);
    p_job->image_cnt++;
  }

  return true;
}

/* 이미지의 시작 주소를 알아낸다. .elf 와 .hex 는 파일이 알고 있고 .bin 은
   fw.txt 에 적혀 있어야 한다. */
static bool jobImageAddr(const job_t *p_job, const job_image_t *p_img, uint32_t *p_addr)
{
  char path[JOB_PATH_MAX * 2];

  if (p_img->has_addr)
  {
    *p_addr = p_img->addr;
    return true;
  }

  jobPath(p_job, p_img->file, path, sizeof(path));

  if (elfIsElfFile(path))
  {
    elf_t    elf;
    uint32_t lo = 0, hi = 0;
    bool     ok;

    if (elfOpen(&elf, path) == false) return false;
    ok = elfGetLoadRange(&elf, &lo, &hi);
    elfClose(&elf);

    if (ok) { *p_addr = lo; return true; }
    return false;
  }

  if (hexIsHexFile(path))
  {
    static hex_t hex;
    bool ok;

    if (hexOpen(&hex, path) == false) return false;
    ok = true;
    *p_addr = hex.lo;
    hexClose(&hex);
    return ok;
  }

  return false;         // .bin 은 주소를 모른다
}

/* 알고리즘 하나를 올리고 그 담당 이미지를 전부 처리한다.
   .stldr 의 Init 은 타깃 클럭을 건드릴 수 있어서 매번 리셋 후 시작한다. */
static swd_err_t jobRunAlgo(job_t *p_job, job_algo_t *p_ja, const prog_dev_t *p_dev,
                            algo_progress_t cb, void *ctx, bool do_verify,
                            uint32_t *p_bad)
{
  swd_err_t err;
  uint32_t  ram    = p_dev->ram;
  uint32_t  ram_sz = p_dev->ram_sz;

  if (ram == 0 || ram_sz == 0) return SWD_ERR_PROTOCOL;

  err = swdCmResetHalt();
  if (err != SWD_OK) return err;

  if (algoOpen(&p_ja->algo, p_ja->path) == false) return SWD_ERR_PROTOCOL;
  p_ja->algo.dev.sz_dev = p_ja->sz_dev;         // 좁힌 범위를 다시 적용한다

  algoSetPSize(&p_ja->algo, p_job->has_psize ? p_job->psize : ALGO_PSIZE_8);

  err = algoLoad(&p_ja->algo, ram, ram_sz);
  if (err != SWD_OK) { algoClose(&p_ja->algo); return err; }

  if (cb) cb("algo", p_ja->algo.dev.dev_adr, p_ja->algo.dev.sz_dev, 0, ctx);

  for (uint32_t i = 0; i < p_ja->img_cnt; i++)
  {
    job_image_t *p_img = &p_job->image[p_ja->img[i]];
    char         path[JOB_PATH_MAX * 2];
    algo_time_t  tm;
    uint32_t     written = 0;
    uint32_t     bad = 0;

    jobPath(p_job, p_img->file, path, sizeof(path));

    if (elfIsElfFile(path))
    {
      err = algoWriteElf(&p_ja->algo, path, cb, ctx, &written, &tm, &p_img->addr);
      if (err == SWD_OK && do_verify) err = algoVerifyElf(&p_ja->algo, path, cb, ctx, &bad);
    }
    else if (hexIsHexFile(path))
    {
      err = algoWriteHex(&p_ja->algo, path, cb, ctx, &written, &tm, &p_img->addr);
      if (err == SWD_OK && do_verify) err = algoVerifyHex(&p_ja->algo, path, cb, ctx, &bad);
    }
    else
    {
      err = algoWriteFile(&p_ja->algo, path, p_img->addr, cb, ctx, &written, &tm);
      if (err == SWD_OK && do_verify)
      {
        err = algoVerifyFile(&p_ja->algo, path, p_img->addr, cb, ctx, &bad);
      }
    }

    if (err != SWD_OK) { algoClose(&p_ja->algo); return err; }
    *p_bad += bad;
  }

  algoClose(&p_ja->algo);
  return SWD_OK;
}


#endif
