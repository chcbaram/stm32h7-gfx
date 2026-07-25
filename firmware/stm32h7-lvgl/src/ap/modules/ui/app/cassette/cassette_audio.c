#include "cassette_audio.h"


#ifdef _USE_HW_LVGL
#include "files.h"
#include "ff_gen_drv.h"
#include "i2s.h"
#include "pdm.h"
#include "mem.h"
#include "arm_math.h"
#include "minimp3.h"


#define WAV_DIR             "/wav"
#define TAPE_MAX_CNT        32
#define TAPE_NAME_MAX       48

#define FFT_SIZE            1024    /* 43Hz/bin@44.1k — 저역 분해능 확보 */
#define SPEC_GAIN           60.0f   /* 스펙트럼 진폭 → dB 스케일 이득 */
#define SPEC_TILT           2.2f    /* 고역 틸트: 막대(≈옥타브)당 dB 부스트 (곱셈) */
#define SPEC_FLOOR          16.0f   /* 이 dB 이하는 바닥으로 깎는다 (변화량 강조) */
#define SPEC_EXPAND         1.8f    /* 바닥 위 구간을 확장하는 배율 */
#define SPEC_BAND_TOP       440     /* 밴드 매핑 상한 bin (~19kHz@44.1k, 1024점 기준).
                                     * 값이 클수록 넓은 대역을 담고, 작을수록 좁은 대역에
                                     * 재분배되어 구분감↑. 하한은 bin1(~43Hz). FFT_SIZE/2 클램프. */
#define REC_MAX_MS          (15*1000)
#define REC_SAMPLE_RATE     16000


typedef struct
{
  char     name[TAPE_NAME_MAX];      /* 표시용 (확장자 뺀 파일명) */
  char     path[TAPE_NAME_MAX + 12];  /* S:/wav/xxx.wav 아님, FatFs 경로 */
  uint32_t size;                      /* 파일 크기 (bytes) */
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
static void doPlayMp3(int idx);
static uint32_t mp3XingDurMs(const uint8_t *buf, int len);
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
static float             fft_win[FFT_SIZE];               /* Hann 창함수 */
static uint16_t          band_lo[CASS_SPECTRUM_BARS];     /* 막대별 시작 bin */
static uint16_t          band_hi[CASS_SPECTRUM_BARS];     /* 막대별 끝 bin (exclusive) */
static int               fft_cnt = 0;
static arm_rfft_fast_instance_f32 fft_inst;

/* 녹음 버퍼 */
static pcm_data_t *rec_buf = NULL;
static uint32_t    rec_cap = 0;

/* MP3 디코더 (한 번에 하나만 재생하므로 정적으로 둔다. mp3dec_t 는 ~6.7KB) */
static mp3dec_t  mp3d;
static uint8_t   mp3_in[8*1024];                            /* 입력 스트림 버퍼 */
static int16_t   mp3_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];    /* 디코드 출력 */
static int16_t   mp3_out[MINIMP3_MAX_SAMPLES_PER_FRAME];    /* i2s 용 스테레오 */
static int16_t   mp3_mono[MINIMP3_MAX_SAMPLES_PER_FRAME/2]; /* 스펙트럼용 모노 */




bool cassetteAudioInit(void)
{
  arm_rfft_fast_init_f32(&fft_inst, FFT_SIZE);
  memset((void *)spectrum, 0, sizeof(spectrum));

  /* Hann 창함수 (스펙트럼 누설을 줄여 밴드 구분을 선명하게 한다) */
  for (int i = 0; i < FFT_SIZE; i++)
    fft_win[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (FFT_SIZE - 1));

  /* 로그(옥타브) 간격 밴드 경계. 저역에 막대를 더 촘촘히 배분해
   * 베이스/중역/고역이 각자 막대를 갖게 한다. bin 1(=DC 제외) ~ N/2.
   */
  {
    float lo = 1.0f;
    float hi = (float)SPEC_BAND_TOP;

    if (hi > FFT_SIZE / 2)
      hi = FFT_SIZE / 2;

    for (int b = 0; b < CASS_SPECTRUM_BARS; b++)
    {
      int i0 = (int)(lo * powf(hi / lo, (float)b       / CASS_SPECTRUM_BARS) + 0.5f);
      int i1 = (int)(lo * powf(hi / lo, (float)(b + 1) / CASS_SPECTRUM_BARS) + 0.5f);

      if (i0 < 1)            i0 = 1;
      if (i1 <= i0)          i1 = i0 + 1;
      if (i1 > FFT_SIZE / 2) i1 = FFT_SIZE / 2;

      band_lo[b] = i0;
      band_hi[b] = i1;
    }
  }

  cassetteAudioScan();

#ifdef _USE_HW_CLI
  cliAdd("cass", cliCmd);
#endif
  /* minimp3 스크래치(~17KB)는 정적 버퍼로 옮겼으므로(minimp3.h 로컬 패치)
   * 스택은 기존대로 8KB 면 충분하다. */
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
    if (ext == NULL ||
        (strcasecmp(ext, ".wav") != 0 && strcasecmp(ext, ".mp3") != 0))
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
    t->size = fno.fsize;

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

uint32_t cassetteAudioSize(int idx)
{
  if (idx < 0 || idx >= tape_cnt)
    return 0;
  return tapes[idx].size;
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
  const char *ext = strrchr(tapes[idx].path, '.');


  /* 확장자로 디코더를 고른다. */
  if (ext != NULL && strcasecmp(ext, ".mp3") == 0)
  {
    doPlayMp3(idx);
    return;
  }

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

/* 첫 프레임의 Xing/Info(VBR) 헤더를 찾아 정확한 재생 길이(ms)를 계산한다.
 * 헤더가 없으면 0 을 반환한다 (그 경우 비트레이트로 추정한다).
 *
 * MP3 프레임 헤더에서 버전/레이어/샘플레이트/채널을 읽어 side-info 크기를
 * 구하고, 그 뒤의 "Xing"/"Info" 태그에서 총 프레임 수를 얻는다.
 *   길이 = 프레임수 * (프레임당 샘플수) / 샘플레이트
 */
uint32_t mp3XingDurMs(const uint8_t *buf, int len)
{
  static const int sr_tab[4][3] = {
    {11025, 12000,  8000},   /* MPEG2.5 */
    {0, 0, 0},               /* reserved */
    {22050, 24000, 16000},   /* MPEG2   */
    {44100, 48000, 32000},   /* MPEG1   */
  };
  int start = 0;

  /* ID3v2 태그가 있으면 건너뛴다. 태그가 크면(앨범아트 등) 첫 프레임이
   * 버퍼 밖이라 못 찾고 0 을 반환한다 (그 경우 비트레이트 추정으로 폴백). */
  if (len > 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3')
  {
    int sz = ((buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) |
             ((buf[8] & 0x7f) << 7)  |  (buf[9] & 0x7f);
    start = 10 + sz;
    if (buf[5] & 0x10)      /* footer present */
      start += 10;
  }

  for (int i = start; i + 12 < len && i < start + 1024; i++)
  {
    int ver, layer, mono, sri, hz, si, xo, spf;
    uint32_t flags, frames;

    /* 프레임 싱크 (11 비트) */
    if (buf[i] != 0xFF || (buf[i+1] & 0xE0) != 0xE0)
      continue;

    ver   = (buf[i+1] >> 3) & 0x03;   /* 0=2.5 1=예약 2=v2 3=v1 */
    layer = (buf[i+1] >> 1) & 0x03;   /* 1=Layer3 */
    if (ver == 1 || layer != 1)
      continue;

    sri = (buf[i+2] >> 2) & 0x03;
    if (sri == 3)
      continue;
    hz = sr_tab[ver][sri];
    if (hz == 0)
      continue;

    mono = (((buf[i+3] >> 6) & 0x03) == 3);

    /* side-info 크기 : MPEG1 → 스테레오32/모노17, MPEG2·2.5 → 17/9 */
    if (ver == 3)
      si = mono ? 17 : 32;
    else
      si = mono ? 9  : 17;

    xo = i + 4 + si;
    if (xo + 12 > len)
      return 0;

    if (memcmp(&buf[xo], "Xing", 4) != 0 && memcmp(&buf[xo], "Info", 4) != 0)
      continue;   /* 이 프레임엔 없음 — 계속 스캔 */

    flags = ((uint32_t)buf[xo+4] << 24) | ((uint32_t)buf[xo+5] << 16) |
            ((uint32_t)buf[xo+6] << 8)  |  (uint32_t)buf[xo+7];
    if ((flags & 0x0001) == 0)
      return 0;   /* 프레임 수 필드 없음 */

    frames = ((uint32_t)buf[xo+8]  << 24) | ((uint32_t)buf[xo+9]  << 16) |
             ((uint32_t)buf[xo+10] << 8)  |  (uint32_t)buf[xo+11];

    spf = (ver == 3) ? 1152 : 576;    /* Layer3 프레임당 샘플수 */
    return (uint32_t)((uint64_t)frames * spf * 1000 / hz);
  }
  return 0;
}

/* minimp3 스트리밍 재생.
 * 8KB 입력 버퍼를 채워 프레임 단위로 디코드하고, 소비한 만큼 당겨가며 진행한다.
 * mp3dec_t/버퍼는 정적(위 참고) 이라 스레드 스택을 넘지 않는다.
 */
void doPlayMp3(int idx)
{
  FILE     *fp;
  size_t    in_len      = 0;
  uint32_t  file_remain = tapes[idx].size;
  int8_t    i2s_ch      = -1;
  bool      started     = false;
  bool      dur_known   = false;  /* Xing/Info 로 정확한 길이를 얻었는가 */
  bool      first_fill  = true;
  uint32_t  rate        = 0;
  uint64_t  played      = 0;    /* 디코드한 총 (채널당) 샘플 수 */


  fp = fopen(tapes[idx].path, "r");
  if (fp == NULL)
    return;

  mp3dec_init(&mp3d);

  cur_idx = idx;
  pos_ms  = 0;
  dur_ms  = 0;
  fft_cnt = 0;
  state   = CASS_AUDIO_PLAY;

  while (state == CASS_AUDIO_PLAY && req == REQ_NONE)
  {
    mp3dec_frame_info_t info;
    int samples;

    /* 입력 버퍼 보충 (남은 파일 바이트로 제한 — ob_fread 는 EOF 짧은카운트가 없다) */
    if (in_len < sizeof(mp3_in) && file_remain > 0)
    {
      uint32_t want = sizeof(mp3_in) - in_len;
      int      n;

      if (want > file_remain)
        want = file_remain;

      n = fread(mp3_in + in_len, 1, want, fp);
      if (n > 0)
      {
        in_len      += n;
        file_remain -= n;
      }
    }
    if (in_len == 0)
      break;   /* 파일 끝 */

    /* 첫 채움에서 VBR 헤더로 정확한 길이를 시도한다. */
    if (first_fill)
    {
      uint32_t d = mp3XingDurMs(mp3_in, (int)in_len);

      first_fill = false;
      if (d > 0)
      {
        dur_ms    = d;
        dur_known = true;
      }
    }

    samples = mp3dec_decode_frame(&mp3d, mp3_in, (int)in_len, mp3_pcm, &info);

    if (info.frame_bytes == 0)
      break;   /* 더 이상 프레임을 못 찾음 (끝) */

    /* 소비한 바이트만큼 앞으로 당긴다. */
    memmove(mp3_in, mp3_in + info.frame_bytes, in_len - info.frame_bytes);
    in_len -= info.frame_bytes;

    if (samples <= 0)
      continue;   /* ID3 / 무효 프레임 건너뜀 */

    if (started == false)
    {
      rate    = info.hz;
      i2sSetSampleRate(rate);
      i2s_ch  = i2sGetEmptyChannel();
      started = true;

      /* Xing/Info 로 못 얻었으면 비트레이트로 추정한다. */
      if (dur_known == false && info.bitrate_kbps > 0)
        dur_ms = (uint32_t)((uint64_t)tapes[idx].size * 8 / info.bitrate_kbps);
    }

    /* 스테레오 인터리브 + 스펙트럼용 모노 구성 */
    for (int i = 0; i < samples; i++)
    {
      int16_t l = (info.channels == 2) ? mp3_pcm[i*2 + 0] : mp3_pcm[i];
      int16_t r = (info.channels == 2) ? mp3_pcm[i*2 + 1] : mp3_pcm[i];
      mp3_out[i*2 + 0] = l;
      mp3_out[i*2 + 1] = r;
      mp3_mono[i] = (int16_t)(((int32_t)l + r) / 2);
    }
    feedSpectrum(mp3_mono, samples);

    /* i2s 여유만큼 나눠 보낸다. (length 단위 = int16 슬롯, 스테레오면 쌍*2) */
    {
      int off = 0;                   /* 스테레오 쌍 단위 오프셋 */

      while (off < samples && state == CASS_AUDIO_PLAY && req == REQ_NONE)
      {
        uint32_t avail_pairs = i2sAvailableForWrite(i2s_ch) / 2;
        int      n;

        if (avail_pairs == 0)
        {
          delay(1);
          continue;
        }

        n = samples - off;
        if ((uint32_t)n > avail_pairs)
          n = (int)avail_pairs;

        i2sWrite(i2s_ch, &mp3_out[off*2], (uint32_t)n * 2);
        off += n;
      }
    }

    played += samples;
    if (rate > 0)
      pos_ms = (uint32_t)(played * 1000 / rate);
  }

  fclose(fp);

  if (state == CASS_AUDIO_PLAY)
  {
    /* 끝까지 재생됨 */
    state  = CASS_AUDIO_IDLE;
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
      /* 1024점이라 스택 대신 정적 버퍼를 쓴다 (단일 스레드 호출) */
      static float out[FFT_SIZE];
      static float mag[FFT_SIZE/2];

      /* 창함수 적용 (누설 감소) 후 FFT */
      for (int k = 0; k < FFT_SIZE; k++)
        fft_acc[k] *= fft_win[k];

      arm_rfft_fast_f32(&fft_inst, fft_acc, out, 0);
      arm_cmplx_mag_f32(out, mag, FFT_SIZE/2);
      fft_cnt = 0;

      /* 로그 밴드별로 평균 세기를 구한다. (막대 폭이 달라 합이 아니라 평균) */
      for (int b = 0; b < CASS_SPECTRUM_BARS; b++)
      {
        float sum = 0;
        int   n   = band_hi[b] - band_lo[b];

        for (int k = band_lo[b]; k < band_hi[b]; k++)
          sum += mag[k];
        if (n < 1) n = 1;

        /* 고역 틸트는 곱셈으로 준다(dB→선형 배수). 신호가 없으면 0 이 유지되어
         * 예전처럼 고역에 상시 바닥이 생기지 않는다. 음악은 고역으로 갈수록
         * 에너지가 롤오프하므로 이를 보정해 고역 막대도 살아나게 한다.
         * (볼륨 적용 전 PCM 기준 = 소스 기반, 볼륨과 무관하게 일정) */
        float tilt = powf(10.0f, (SPEC_TILT * b) / 20.0f);
        float db   = 20.0f * log10f((sum / n) * SPEC_GAIN * tilt + 1.0f);

        /* 바닥(SPEC_FLOOR)을 빼고 확장해 조용/큰 구간의 변화량을 키운다. */
        int v = (int)((db - SPEC_FLOOR) * SPEC_EXPAND);
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
