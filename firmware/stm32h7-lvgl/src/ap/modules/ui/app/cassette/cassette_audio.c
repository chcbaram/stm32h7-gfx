#include "cassette_audio.h"


#ifdef _USE_HW_LVGL
#include "files.h"
#include "ff_gen_drv.h"
#include "i2s.h"
#include "pdm.h"
#include "mem.h"
#include "arm_math.h"


#define WAV_DIR             "/wav"
#define TAPE_MAX_CNT        32
#define TAPE_NAME_MAX       48

#define FFT_SIZE            256
#define REC_MAX_MS          (15*1000)
#define REC_SAMPLE_RATE     16000


typedef struct
{
  char     name[TAPE_NAME_MAX];      /* 표시용 (확장자 뺀 파일명) */
  char     path[TAPE_NAME_MAX + 12];  /* S:/wav/xxx.wav 아님, FatFs 경로 */
} tape_t;

typedef enum
{
  REQ_NONE = 0,
  REQ_PLAY,
  REQ_REC,
  REQ_STOP,
} audio_req_t;


static void cassetteAudioThread(void const *arg);
static bool wavOpen(const char *path, uint32_t *p_rate, uint16_t *p_ch, uint32_t *p_data_len);
static void doPlay(int idx);
static void doRecord(void);
static void doStop(void);
static void feedSpectrum(const int16_t *p_mono, int cnt);
static void recWriteWav(const char *path, pcm_data_t *p_pcm, uint32_t samples, uint32_t rate);
#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif


static tape_t   tapes[TAPE_MAX_CNT];
static int      tape_cnt = 0;

static volatile CassAudioState_t state = CASS_AUDIO_IDLE;
static volatile int      cur_idx = -1;
static volatile uint32_t pos_ms  = 0;
static volatile uint32_t dur_ms  = 0;

static volatile audio_req_t req = REQ_NONE;
static volatile int         req_idx = 0;

/* 스펙트럼 (스레드가 쓰고 UI 가 읽는다) */
static volatile uint8_t  spectrum[CASS_SPECTRUM_BARS];
static float             fft_acc[FFT_SIZE];
static int               fft_cnt = 0;
static arm_rfft_fast_instance_f32 fft_inst;

/* 녹음 버퍼 */
static pcm_data_t *rec_buf = NULL;
static uint32_t    rec_cap = 0;




bool cassetteAudioInit(void)
{
  arm_rfft_fast_init_f32(&fft_inst, FFT_SIZE);
  memset((void *)spectrum, 0, sizeof(spectrum));

  cassetteAudioScan();

#ifdef _USE_HW_CLI
  cliAdd("cass", cliCmd);
#endif
  return threadCreate("cass_audio", cassetteAudioThread, NULL, osPriorityNormal, 8*1024);
}

int cassetteAudioScan(void)
{
  DIR dir;
  FILINFO fno;


  tape_cnt = 0;

  if (f_opendir(&dir, WAV_DIR) != FR_OK)
    return 0;

  while (tape_cnt < TAPE_MAX_CNT)
  {
    if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
      break;

    if (fno.fattrib & AM_DIR)
      continue;

    const char *ext = strrchr(fno.fname, '.');
    if (ext == NULL || (strcasecmp(ext, ".wav") != 0))
      continue;

    tape_t *t = &tapes[tape_cnt];

    /* 파일명이 버퍼보다 길면 건너뛴다. (snprintf 대신 직접 조립) */
    if (strlen(fno.fname) + sizeof(WAV_DIR) + 1 >= sizeof(t->path))
      continue;

    strcpy(t->path, WAV_DIR "/");
    strcat(t->path, fno.fname);

    /* 표시 이름은 확장자를 뗀다. */
    uint32_t len = ext - fno.fname;
    if (len >= sizeof(t->name))
      len = sizeof(t->name) - 1;
    memcpy(t->name, fno.fname, len);
    t->name[len] = 0;

    tape_cnt++;
  }

  f_closedir(&dir);
  return tape_cnt;
}

int cassetteAudioCount(void)
{
  return tape_cnt;
}

const char *cassetteAudioName(int idx)
{
  if (idx < 0 || idx >= tape_cnt)
    return NULL;
  return tapes[idx].name;
}

bool cassetteAudioPlay(int idx)
{
  if (idx < 0 || idx >= tape_cnt)
    return false;

  req_idx = idx;
  req     = REQ_PLAY;
  return true;
}

bool cassetteAudioRecord(void)
{
  req = REQ_REC;
  return true;
}

bool cassetteAudioStop(void)
{
  req = REQ_STOP;
  return true;
}

CassAudioState_t cassetteAudioGetState(void) { return state; }
int      cassetteAudioGetIndex(void)         { return cur_idx; }
uint32_t cassetteAudioGetPosMs(void)         { return pos_ms; }
uint32_t cassetteAudioGetDurMs(void)         { return dur_ms; }

int cassetteAudioGetVolume(void)
{
  return i2sGetVolume();
}

void cassetteAudioSetVolume(int vol)
{
  if (vol < 0)   vol = 0;
  if (vol > 100) vol = 100;
  i2sSetVolume(vol);
}

int cassetteAudioGetSpectrum(uint8_t *p_bars, int max_bars)
{
  int n = max_bars < CASS_SPECTRUM_BARS ? max_bars : CASS_SPECTRUM_BARS;

  for (int i = 0; i < n; i++)
  {
    p_bars[i] = spectrum[i];
  }
  return n;
}

/* --------------------------------------------------------------------------
 * 오디오 스레드
 * ----------------------------------------------------------------------- */
void cassetteAudioThread(void const *arg)
{
  UNUSED(arg);

  while(1)
  {
    audio_req_t r = req;

    if (r != REQ_NONE)
    {
      req = REQ_NONE;

      if (state != CASS_AUDIO_IDLE)
        doStop();

      if (r == REQ_PLAY)  doPlay(req_idx);
      if (r == REQ_REC)   doRecord();
    }

    delay(2);
  }
}

/* wav 파일에서 fmt/data 청크를 찾아 재생 정보를 얻는다.
 * 파일 포인터는 data 청크의 첫 바이트에 놓인 채로 돌려준다.
 */
bool wavOpen(const char *path, uint32_t *p_rate, uint16_t *p_ch, uint32_t *p_data_len)
{
  FILE *fp = fopen(path, "r");
  uint8_t hdr[12];
  uint16_t ch = 2;
  uint32_t rate = 44100;
  uint16_t bits = 16;
  bool got_fmt = false;


  if (fp == NULL)
    return false;

  if (fread(hdr, 1, 12, fp) != 12 ||
      memcmp(&hdr[0], "RIFF", 4) != 0 || memcmp(&hdr[8], "WAVE", 4) != 0)
  {
    fclose(fp);
    return false;
  }

  /* 청크를 순회한다. */
  while (1)
  {
    uint8_t ck[8];
    uint32_t ck_len;

    if (fread(ck, 1, 8, fp) != 8)
      break;

    ck_len = ck[4] | (ck[5] << 8) | (ck[6] << 16) | (ck[7] << 24);

    if (memcmp(ck, "fmt ", 4) == 0)
    {
      uint8_t fmt[16];
      uint32_t rd = ck_len < 16 ? ck_len : 16;

      fread(fmt, 1, rd, fp);
      ch   = fmt[2] | (fmt[3] << 8);
      rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
      bits = fmt[14] | (fmt[15] << 8);
      got_fmt = true;

      if (ck_len > rd)
        fseek(fp, ftell(fp) + (ck_len - rd), SEEK_SET);
    }
    else if (memcmp(ck, "data", 4) == 0)
    {
      if (got_fmt == false || bits != 16 || ch == 0)
      {
        fclose(fp);
        return false;
      }
      *p_rate     = rate;
      *p_ch       = ch;
      *p_data_len = ck_len;
      /* 파일 스트림을 그대로 넘길 수 없어 닫고, 재생부에서 다시 연다.
       * 여기서는 검증만 하고 위치는 재생부가 다시 잡는다.
       */
      fclose(fp);
      return true;
    }
    else
    {
      fseek(fp, ftell(fp) + ck_len, SEEK_SET);
    }
  }

  fclose(fp);
  return false;
}

void doPlay(int idx)
{
  uint32_t rate;
  uint16_t ch;
  uint32_t data_len;
  FILE    *fp;
  int8_t   i2s_ch;
  uint32_t byte_per_sample;


  if (wavOpen(tapes[idx].path, &rate, &ch, &data_len) != true)
    return;

  /* data 청크 시작 위치를 다시 찾는다. */
  fp = fopen(tapes[idx].path, "r");
  if (fp == NULL)
    return;
  {
    uint8_t hdr[12];
    fread(hdr, 1, 12, fp);
    while (1)
    {
      uint8_t ck[8];
      uint32_t ck_len;
      if (fread(ck, 1, 8, fp) != 8) { fclose(fp); return; }
      ck_len = ck[4] | (ck[5] << 8) | (ck[6] << 16) | (ck[7] << 24);
      if (memcmp(ck, "data", 4) == 0) { break; }
      fseek(fp, ftell(fp) + ck_len, SEEK_SET);
    }
  }

  byte_per_sample = 2 * ch;
  dur_ms  = (uint32_t)((uint64_t)(data_len / byte_per_sample) * 1000 / rate);
  pos_ms  = 0;
  cur_idx = idx;
  fft_cnt = 0;

  i2sSetSampleRate(rate);
  i2s_ch = i2sGetEmptyChannel();
  state  = CASS_AUDIO_PLAY;

  {
    uint32_t frame = i2sGetFrameSize();   /* 스테레오 샘플 수 */
    int16_t  rd_buf[frame * 2];           /* 최대 스테레오 원본 */
    int16_t  wr_buf[frame * 2];           /* i2s 로 보낼 스테레오 */
    int16_t  mono[frame];
    uint32_t sent_bytes = 0;
    uint32_t data_remain = data_len;      /* data 청크만큼만 읽는다 */

    while (state == CASS_AUDIO_PLAY && req == REQ_NONE && data_remain > 0)
    {
      uint32_t half = frame / 2;          /* 프레임당 모노 샘플 수 */
      uint32_t want = half;

      /* ob_fread 가 EOF 에서 짧은 카운트를 주지 않으므로
       * 남은 data 바이트로 직접 제한한다.
       */
      if (want * byte_per_sample > data_remain)
        want = data_remain / byte_per_sample;
      if (want == 0)
        break;

      if (i2sAvailableForWrite(i2s_ch) < i2sGetFrameSize())
      {
        delay(1);
        continue;
      }

      int rd = fread(rd_buf, byte_per_sample, want, fp);
      if (rd <= 0)
        break;
      if ((uint32_t)rd > want)      /* ob_fread 가 요청보다 많이 줄 때 방어 */
        rd = (int)want;

      {
        uint32_t consumed = (uint32_t)rd * byte_per_sample;
        if (consumed >= data_remain) data_remain = 0;
        else                         data_remain -= consumed;
      }

      for (int i = 0; i < rd; i++)
      {
        int16_t l = (ch == 2) ? rd_buf[i*2 + 0] : rd_buf[i];
        int16_t r = (ch == 2) ? rd_buf[i*2 + 1] : rd_buf[i];
        wr_buf[i*2 + 0] = l;
        wr_buf[i*2 + 1] = r;
        mono[i] = (int16_t)(((int32_t)l + r) / 2);
      }

      i2sWrite(i2s_ch, wr_buf, rd * 2);
      feedSpectrum(mono, rd);

      sent_bytes += rd * byte_per_sample;
      pos_ms = (uint32_t)((uint64_t)(sent_bytes / byte_per_sample) * 1000 / rate);
    }
  }

  fclose(fp);

  if (state == CASS_AUDIO_PLAY)
  {
    /* 곡이 끝까지 재생됨 */
    state = CASS_AUDIO_IDLE;
    pos_ms = dur_ms;
    memset((void *)spectrum, 0, sizeof(spectrum));
  }
}

void doRecord(void)
{
  uint32_t cap_samples = (uint32_t)REC_SAMPLE_RATE * (REC_MAX_MS / 1000);
  char path[TAPE_NAME_MAX + 12];


  if (rec_buf == NULL)
  {
    rec_cap = cap_samples;
    rec_buf = (pcm_data_t *)memMalloc(rec_cap * sizeof(pcm_data_t));
    if (rec_buf == NULL)
      return;
  }

  state  = CASS_AUDIO_REC;
  dur_ms = REC_MAX_MS;
  pos_ms = 0;
  cur_idx = -1;

  pdmBegin();
  pdmRecordStart(rec_buf, rec_cap);

  while (state == CASS_AUDIO_REC && req == REQ_NONE)
  {
    uint32_t len = pdmRecordGetLength();

    pos_ms = (uint32_t)((uint64_t)len * 1000 / pdmGetSampleRate());

    /* 녹음중 마이크 입력으로 스펙트럼을 만든다. */
    if (len > FFT_SIZE)
    {
      int16_t mono[FFT_SIZE];
      for (int i = 0; i < FFT_SIZE; i++)
        mono[i] = rec_buf[len - FFT_SIZE + i].L;
      feedSpectrum(mono, FFT_SIZE);
    }

    if (pdmRecordIsDone() == true)
      break;

    delay(20);
  }

  pdmRecordStop();
  pdmEnd();

  uint32_t rec_len = pdmRecordGetLength();
  if (rec_len > 0)
  {
    /* 파일명은 rec_0001.wav 형태로 빈 번호를 찾는다. */
    for (int n = 1; n < 1000; n++)
    {
      FILINFO fno;

      snprintf(path, sizeof(path), "%s/rec_%04d.wav", WAV_DIR, n);
      if (f_stat(path, &fno) != FR_OK)
        break;    /* 없는 번호 */
    }
    recWriteWav(path, rec_buf, rec_len, pdmGetSampleRate());
    cassetteAudioScan();
  }

  state = CASS_AUDIO_IDLE;
  memset((void *)spectrum, 0, sizeof(spectrum));
}

void doStop(void)
{
  state = CASS_AUDIO_IDLE;
  memset((void *)spectrum, 0, sizeof(spectrum));
}

/* 모노 샘플을 모아 FFT_SIZE 만큼 차면 스펙트럼을 갱신한다. */
void feedSpectrum(const int16_t *p_mono, int cnt)
{
  for (int i = 0; i < cnt; i++)
  {
    fft_acc[fft_cnt++] = (float)p_mono[i] / 32768.0f;

    if (fft_cnt >= FFT_SIZE)
    {
      float out[FFT_SIZE];
      float mag[FFT_SIZE/2];

      arm_rfft_fast_f32(&fft_inst, fft_acc, out, 0);
      arm_cmplx_mag_f32(out, mag, FFT_SIZE/2);
      fft_cnt = 0;

      /* 절반 스펙트럼을 막대 개수로 뭉친다. DC 는 버린다. */
      int per = (FFT_SIZE/2 - 1) / CASS_SPECTRUM_BARS;
      if (per < 1) per = 1;

      for (int b = 0; b < CASS_SPECTRUM_BARS; b++)
      {
        float peak = 0;
        for (int j = 0; j < per; j++)
        {
          int k = 1 + b * per + j;
          if (k < FFT_SIZE/2 && mag[k] > peak)
            peak = mag[k];
        }

        /* 로그 스케일로 0..100 에 맞춘다. */
        int v = (int)(20.0f * log10f(peak * 40.0f + 1.0f));
        if (v < 0)   v = 0;
        if (v > 100) v = 100;

        /* 부드럽게 떨어지도록 감쇠 */
        int prev = spectrum[b];
        if (v >= prev) spectrum[b] = v;
        else           spectrum[b] = prev - ((prev - v) / 3) - 1;
      }
    }
  }
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *st[] = {"IDLE", "PLAY", "REC"};
    uint8_t bars[CASS_SPECTRUM_BARS];

    cliPrintf("count : %d\n", tape_cnt);
    for (int i = 0; i < tape_cnt; i++)
      cliPrintf("  %d : %s\n", i, tapes[i].name);
    cliPrintf("state : %s\n", st[state]);
    cliPrintf("pos   : %d / %d ms\n", (int)pos_ms, (int)dur_ms);

    cassetteAudioGetSpectrum(bars, CASS_SPECTRUM_BARS);
    cliPrintf("spec  :");
    for (int i = 0; i < CASS_SPECTRUM_BARS; i++)
      cliPrintf(" %d", bars[i]);
    cliPrintf("\n");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "play"))
  {
    cassetteAudioPlay(args->getData(1));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "stop"))
  {
    cassetteAudioStop();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rec"))
  {
    cassetteAudioRecord();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "scan"))
  {
    cliPrintf("found %d\n", cassetteAudioScan());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("cass info\n");
    cliPrintf("cass play [idx]\n");
    cliPrintf("cass stop\n");
    cliPrintf("cass rec\n");
    cliPrintf("cass scan\n");
  }
}
#endif

/* pcm_data_t{R,L} 버퍼를 16bit mono wav 로 저장한다. */
void recWriteWav(const char *path, pcm_data_t *p_pcm, uint32_t samples, uint32_t rate)
{
  FILE *fp = fopen(path, "w");
  uint32_t data_len = samples * 2;   /* mono 16bit */
  uint8_t  h[44];


  if (fp == NULL)
    return;

  memcpy(&h[0], "RIFF", 4);
  uint32_t riff = 36 + data_len;
  h[4]=riff; h[5]=riff>>8; h[6]=riff>>16; h[7]=riff>>24;
  memcpy(&h[8], "WAVE", 4);
  memcpy(&h[12], "fmt ", 4);
  h[16]=16; h[17]=0; h[18]=0; h[19]=0;      /* fmt 크기 */
  h[20]=1;  h[21]=0;                        /* PCM      */
  h[22]=1;  h[23]=0;                        /* mono     */
  h[24]=rate; h[25]=rate>>8; h[26]=rate>>16; h[27]=rate>>24;
  uint32_t byte_rate = rate * 2;
  h[28]=byte_rate; h[29]=byte_rate>>8; h[30]=byte_rate>>16; h[31]=byte_rate>>24;
  h[32]=2; h[33]=0;                         /* block align */
  h[34]=16; h[35]=0;                        /* bits        */
  memcpy(&h[36], "data", 4);
  h[40]=data_len; h[41]=data_len>>8; h[42]=data_len>>16; h[43]=data_len>>24;

  fwrite(h, 1, 44, fp);

  /* L 채널만 뽑아 덩어리로 쓴다. */
  {
    int16_t chunk[512];
    uint32_t i = 0;
    while (i < samples)
    {
      uint32_t n = (samples - i) < 512 ? (samples - i) : 512;
      for (uint32_t j = 0; j < n; j++)
        chunk[j] = p_pcm[i + j].L;
      fwrite(chunk, 2, n, fp);
      i += n;
    }
  }

  fclose(fp);
}

#endif
