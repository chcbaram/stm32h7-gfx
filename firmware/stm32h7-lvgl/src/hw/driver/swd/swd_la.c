/*
 * swd_la.c
 *
 *  SWD 내장 로직 애널라이저
 *
 *  TIM2 가 만드는 샘플 클럭에 맞춰 DMA2 가 GPIO IDR 을 통째로 읽어 RAM_D2 에
 *  쌓는다. 캡처는 원샷이고, 무장 -> SWD 동작 -> 정지 -> 분석 순서로 쓴다.
 *
 *  왜 TIM 입력 캡처가 아닌가
 *    SWCLK(PE3) / SWDIO(PC10) 둘 다 TIM 대체기능이 없다. 있었더라도 비트뱅잉
 *    중에는 MODER=출력이라 AF 입력 경로가 끊겨서 캡처가 안 된다.
 *    IDR 을 주기 샘플링하면 AF 가 필요 없고, 덤으로 두 선을 동시에 본다.
 *
 *  자원
 *    TIM2          - 32비트, 프리 (TIM6=HAL tick, TIM13=백라이트만 사용 중)
 *    DMA2_Stream0  - TIM2_UP  트리거, SWCLK 포트 IDR
 *    DMA2_Stream1  - TIM2_CH1 트리거, SWDIO 포트 IDR
 *    RAM_D2 32KB   - 원래 아무도 안 쓰던 영역. MPU 리전 5 로 non-cacheable
 *
 *  두 스트림은 같은 타이머의 서로 다른 이벤트라 CCR1 만큼 고정 스큐가 있다.
 *  10~20배 오버샘플링에서는 무시할 수준이다.
 */

#include "swd/swd_la.h"
#include "swd.h"
#include "cli.h"


#ifdef _USE_HW_SWD


#define LA_BUF_CNT        8000      // 채널당 샘플 수. 16000B x 2 = 31.2KB
#define LA_BIT_MAX        2048      // 디코드용 비트 버퍼
#define LA_RATE_KHZ_DEF   20000     // 기본 20 MSPS


static uint16_t la_clk[LA_BUF_CNT] __attribute__((section(".ram_d2"), aligned(32)));
static uint16_t la_dio[LA_BUF_CNT] __attribute__((section(".ram_d2"), aligned(32)));

static uint8_t  la_bit[LA_BIT_MAX];       // 디코드된 비트값
static uint16_t la_bit_idx[LA_BIT_MAX];   // 각 비트의 샘플 인덱스 (LA_BUF_CNT < 65536)
static uint32_t la_bit_cnt;

static TIM_HandleTypeDef htim_la;
static DMA_HandleTypeDef hdma_clk;
static DMA_HandleTypeDef hdma_dio;

static bool     is_init = false;
static bool     is_armed = false;
static bool     auto_trig = false;
static uint32_t la_rate_khz = LA_RATE_KHZ_DEF;
static uint32_t la_count;
static uint32_t la_start;    // 링 버퍼에서 가장 오래된 샘플의 위치

static uint8_t  pin_clk;
static uint8_t  pin_dio;


static uint32_t swdLaTimClk(void);

/* 캡처는 순환 모드로 돈다. 실패 직전 구간을 남기려면 최신 샘플이 살아야 해서
   원샷이 아니라 링이어야 한다. 그래서 접근은 항상 la_start 기준 오프셋이다. */
static inline uint16_t laClk(uint32_t i)
{
  uint32_t k = la_start + i;
  if (k >= LA_BUF_CNT) k -= LA_BUF_CNT;
  return la_clk[k];
}

static inline uint16_t laDio(uint32_t i)
{
  uint32_t k = la_start + i;
  if (k >= LA_BUF_CNT) k -= LA_BUF_CNT;
  return la_dio[k];
}

static void     swdLaStop(void);
static uint32_t swdLaBits(void);


// ----------------------------------------------------------------- 초기화

bool swdLaInit(void)
{
  bool ret = true;

  pin_clk = swdGetClkPin();
  pin_dio = swdGetIoPin();

  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  hdma_clk.Instance                 = DMA2_Stream0;
  hdma_clk.Init.Request             = DMA_REQUEST_TIM2_UP;
  hdma_clk.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_clk.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_clk.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_clk.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_clk.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  hdma_clk.Init.Mode                = DMA_CIRCULAR;
  hdma_clk.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  hdma_clk.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_clk) != HAL_OK)
  {
    ret = false;
  }

  hdma_dio = hdma_clk;
  hdma_dio.Instance     = DMA2_Stream1;
  hdma_dio.Init.Request = DMA_REQUEST_TIM2_CH1;
  if (HAL_DMA_Init(&hdma_dio) != HAL_OK)
  {
    ret = false;
  }

  is_init  = ret;
  is_armed = false;
  la_count = 0;

  logPrintf("[%s] swdLaInit()\n", ret ? "OK" : "NG");
  logPrintf("     buf  : %d x 2 sample (RAM_D2)\n", LA_BUF_CNT);
  logPrintf("     tim  : %d MHz\n", (int)(swdLaTimClk()/1000000));

  return ret;
}

bool swdLaIsInit(void)
{
  return is_init;
}

/* TIM2 는 APB1 이다. APB1 프리스케일러가 1 이 아니면 타이머 클럭은 PCLK1 의
   두 배가 된다. 지금 설정(APB1_DIV2)에서는 137.5MHz x 2 = 275MHz 다. */
uint32_t swdLaTimClk(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

  if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) == RCC_D2CFGR_D2PPRE1_DIV1)
  {
    return pclk1;
  }
  return pclk1 * 2;
}


// ----------------------------------------------------------------- 캡처

void swdLaStop(void)
{
  __HAL_TIM_DISABLE(&htim_la);
  __HAL_TIM_DISABLE_DMA(&htim_la, TIM_DMA_UPDATE);
  __HAL_TIM_DISABLE_DMA(&htim_la, TIM_DMA_CC1);

  HAL_DMA_Abort(&hdma_clk);
  HAL_DMA_Abort(&hdma_dio);
}

bool swdLaArm(uint32_t rate_khz)
{
  uint32_t tim_clk;
  uint32_t period;

  if (is_init == false)
  {
    return false;
  }

  if (rate_khz == 0)
  {
    rate_khz = la_rate_khz;
  }

  swdLaStop();

  tim_clk = swdLaTimClk();
  period  = tim_clk / (rate_khz * 1000);
  if (period < 2)
  {
    period = 2;                 // 타이머가 못 따라가는 요청은 클램프
  }
  la_rate_khz = (tim_clk / period) / 1000;

  htim_la.Instance               = TIM2;
  htim_la.Init.Prescaler         = 0;
  htim_la.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim_la.Init.Period            = period - 1;
  htim_la.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim_la.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim_la) != HAL_OK)
  {
    return false;
  }

  /* CH1 은 핀으로 나가지 않는 순수 타이밍 채널이다. UP 이벤트만으로는 DMA
     요청이 하나뿐이라 두 번째 스트림을 못 돌린다. CCR1 을 주기의 절반에 두면
     매 주기 한 번 확실히 CC1 이 발생한다. */
  {
    TIM_OC_InitTypeDef oc = {0};

    oc.OCMode     = TIM_OCMODE_TIMING;
    oc.Pulse      = period / 2;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_OC_ConfigChannel(&htim_la, &oc, TIM_CHANNEL_1) != HAL_OK)
    {
      return false;
    }
  }

  if (HAL_DMA_Start(&hdma_clk, (uint32_t)swdGetClkIdr(), (uint32_t)la_clk, LA_BUF_CNT) != HAL_OK)
  {
    return false;
  }
  if (HAL_DMA_Start(&hdma_dio, (uint32_t)swdGetIoIdr(), (uint32_t)la_dio, LA_BUF_CNT) != HAL_OK)
  {
    HAL_DMA_Abort(&hdma_clk);
    return false;
  }

  __HAL_TIM_CLEAR_FLAG(&htim_la, TIM_FLAG_UPDATE | TIM_FLAG_CC1);
  __HAL_TIM_ENABLE_DMA(&htim_la, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE_DMA(&htim_la, TIM_DMA_CC1);
  __HAL_TIM_ENABLE(&htim_la);

  is_armed   = true;
  la_count   = 0;
  la_start   = 0;
  la_bit_cnt = 0;

  return true;
}

void swdLaFreeze(void)
{
  uint32_t left_clk;
  uint32_t left_dio;

  if (is_armed == false)
  {
    return;
  }

  __HAL_TIM_DISABLE(&htim_la);          // 요청이 끊기면 DMA 도 멈춘다

  left_clk = __HAL_DMA_GET_COUNTER(&hdma_clk);
  left_dio = __HAL_DMA_GET_COUNTER(&hdma_dio);

  // 두 스트림 중 덜 채워진 쪽을 기준으로 삼는다
  {
    uint32_t left = (left_clk > left_dio) ? left_clk : left_dio;
    uint32_t head = LA_BUF_CNT - left;          // 다음에 쓰일 위치

    if (__HAL_DMA_GET_FLAG(&hdma_clk, __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_clk)))
    {
      // 한 바퀴 이상 돌았다. 버퍼 전체가 유효하고 가장 오래된 건 head 위치.
      la_count = LA_BUF_CNT;
      la_start = head;
    }
    else
    {
      la_count = head;
      la_start = 0;
    }
  }

  swdLaStop();

  is_armed   = false;
  la_bit_cnt = 0;
}

bool swdLaIsDone(void)
{
  if (is_armed == false)
  {
    return true;
  }
  return (__HAL_DMA_GET_COUNTER(&hdma_clk) == 0);
}

uint32_t swdLaCount(void)
{
  return la_count;
}

uint32_t swdLaRate(void)
{
  return la_rate_khz;
}

bool swdLaGet(uint32_t idx, uint8_t *p_clk, uint8_t *p_dio)
{
  if (idx >= la_count)
  {
    return false;
  }

  if (p_clk != NULL) *p_clk = (uint8_t)((laClk(idx) >> pin_clk) & 1);
  if (p_dio != NULL) *p_dio = (uint8_t)((laDio(idx) >> pin_dio) & 1);

  return true;
}

void swdLaAutoSet(bool enable)
{
  auto_trig = enable;
}

bool swdLaAutoGet(void)
{
  return auto_trig;
}

void swdLaAutoTrig(void)
{
  if (auto_trig == true && is_armed == true)
  {
    swdLaFreeze();
    auto_trig = false;      // 첫 실패만 잡고 멈춘다. 덮어쓰지 않게.
  }
}


// ----------------------------------------------------------------- 분석

/* SWCLK 상승 에지마다 비트 하나.
   SWDIO 는 클럭이 low 인 동안 안정되고 상승 에지에서 샘플되므로,
   에지 직전 샘플의 SWDIO 를 그 비트의 값으로 본다. */
uint32_t swdLaBits(void)
{
  uint8_t prev = 0;

  la_bit_cnt = 0;

  if (la_count < 2)
  {
    return 0;
  }

  prev = (uint8_t)((laClk(0) >> pin_clk) & 1);

  for (uint32_t i = 1; i < la_count; i++)
  {
    uint8_t cur = (uint8_t)((laClk(i) >> pin_clk) & 1);

    if (prev == 0 && cur == 1)
    {
      if (la_bit_cnt >= LA_BIT_MAX)
      {
        break;
      }
      la_bit[la_bit_cnt]     = (uint8_t)((laDio(i-1) >> pin_dio) & 1);
      la_bit_idx[la_bit_cnt] = (uint16_t)i;
      la_bit_cnt++;
    }
    prev = cur;
  }

  return la_bit_cnt;
}

bool swdLaAnalyze(swd_la_stat_t *p_stat)
{
  uint32_t ns_per_sample;
  uint32_t p_min = 0xFFFFFFFF;
  uint32_t p_max = 0;
  uint32_t p_sum = 0;
  uint32_t hi_sum = 0;
  uint32_t lo_sum = 0;
  uint32_t prev_rise = 0;
  uint32_t rise_cnt = 0;
  uint8_t  prev;
  uint32_t hi_start = 0;
  uint16_t dio_first;
  bool     dio_moved = false;

  if (p_stat == NULL || la_count < 2)
  {
    return false;
  }

  memset(p_stat, 0, sizeof(swd_la_stat_t));
  p_stat->samples  = la_count;
  p_stat->rate_khz = la_rate_khz;

  ns_per_sample = 1000000UL / la_rate_khz;      // kHz -> ns

  dio_first = (uint16_t)((laDio(0) >> pin_dio) & 1);
  prev      = (uint8_t)((laClk(0) >> pin_clk) & 1);

  for (uint32_t i = 1; i < la_count; i++)
  {
    uint8_t cur = (uint8_t)((laClk(i) >> pin_clk) & 1);

    if (((laDio(i) >> pin_dio) & 1) != dio_first)
    {
      dio_moved = true;
    }

    if (prev != cur)
    {
      if (cur == 1)
      {
        // 상승 에지
        if (rise_cnt > 0)
        {
          uint32_t period = i - prev_rise;

          if (period < p_min) p_min = period;
          if (period > p_max) p_max = period;
          p_sum += period;
          lo_sum += (i - hi_start);       // 직전 low 폭
        }
        prev_rise = i;
        rise_cnt++;
      }
      else
      {
        // 하강 에지 -> 직전 high 폭
        if (rise_cnt > 0)
        {
          hi_sum += (i - prev_rise);
        }
        hi_start = i;
      }
    }
    prev = cur;
  }

  p_stat->edges     = rise_cnt;
  p_stat->clk_moved = (rise_cnt > 0);
  p_stat->dio_moved = dio_moved;

  if (rise_cnt >= 2)
  {
    uint32_t n = rise_cnt - 1;

    p_stat->f_avg_khz = (uint32_t)(((uint64_t)la_rate_khz * n) / p_sum);
    p_stat->f_min_khz = la_rate_khz / p_max;      // 주기가 길수록 느리다
    p_stat->f_max_khz = la_rate_khz / p_min;
    p_stat->jitter_ns = (p_max - p_min) * ns_per_sample;
    p_stat->hi_ns     = (hi_sum * ns_per_sample) / n;
    p_stat->lo_ns     = (lo_sum * ns_per_sample) / n;
  }

  return true;
}


// ----------------------------------------------------------------- 출력

#ifdef _USE_HW_CLI

void swdLaDump(uint32_t count)
{
  char line_c[81];
  char line_d[81];
  uint32_t idx = 0;

  if (la_count == 0)
  {
    cliPrintf("no capture\n");
    return;
  }

  if (count == 0 || count > la_count)
  {
    count = la_count;
  }

  cliPrintf("sample 0 ~ %d  (%d ns/sample)\n", (int)count-1, (int)(1000000UL/la_rate_khz));

  while (idx < count)
  {
    uint32_t n = 0;

    while (n < 80 && idx < count)
    {
      line_c[n] = ((laClk(idx) >> pin_clk) & 1) ? '-' : '_';
      line_d[n] = ((laDio(idx) >> pin_dio) & 1) ? '-' : '_';
      n++;
      idx++;
    }
    line_c[n] = 0;
    line_d[n] = 0;

    cliPrintf("CLK %s\n", line_c);
    cliPrintf("DIO %s\n\n", line_d);
  }
}

/* SWD 프로토콜 디코드.
   Stage 1 의 실패표(turnaround off-by-one, ACK=0b111, 비트 회전)를
   추측이 아니라 눈으로 판정하기 위한 것이다. */
void swdLaDecode(void)
{
  uint32_t i = 0;
  uint32_t ns_per_sample;

  if (swdLaBits() == 0)
  {
    cliPrintf("no clock edge in capture\n");
    return;
  }

  ns_per_sample = 1000000UL / la_rate_khz;
  cliPrintf("bits : %d  (rate %d KHz)\n\n", (int)la_bit_cnt, (int)la_rate_khz);

  while (i < la_bit_cnt)
  {
    uint32_t t_us = (la_bit_idx[i] * ns_per_sample) / 1000;
    uint32_t run;

    // SWDIO=1 이 길게 이어지면 line reset
    if (la_bit[i] == 1)
    {
      run = 0;
      while ((i + run) < la_bit_cnt && la_bit[i + run] == 1) run++;

      if (run >= 50)
      {
        cliPrintf("%6dus  line reset : SWDIO=1, %d clk  OK\n", (int)t_us, (int)run);
        i += run;

        /* JTAG -> SWD 매직은 line reset 바로 뒤에만 온다.
           0xE79E 는 LSB(먼저 나가는 비트)가 0 이라 "비트가 1이면" 으로
           분기하면 영영 만나지 못하고, 거기서 1비트가 밀려 이후 프레임이
           전부 깨진다. 그래서 line reset 직후에 따로 본다. */
        if ((i + 16) <= la_bit_cnt)
        {
          uint32_t magic = 0;

          for (int k = 0; k < 16; k++) magic |= ((uint32_t)la_bit[i+k]) << k;

          if (magic == 0xE79E)
          {
            cliPrintf("%6dus  jtag2swd   : 0xE79E  OK\n",
                      (int)(((uint32_t)la_bit_idx[i] * ns_per_sample) / 1000));
            i += 16;
          }
        }
        continue;
      }

      /* 패킷 */
      if ((i + 8) <= la_bit_cnt)
      {
        uint32_t req = 0;
        uint32_t ap_ndp, rd_nwr, a2, a3, par, park;
        uint32_t par_exp;
        uint32_t ack = 0;

        for (int k = 0; k < 8; k++) req |= ((uint32_t)la_bit[i+k]) << k;

        /* Start=1(b0), Stop=0(b6), Park=1(b7) 이 아니면 요청 프레임이 아니다.
           이 검사가 없으면 한 번 어긋난 뒤로 계속 쓰레기를 뱉는다. */
        if ((req & 0x81) != 0x81 || (req & 0x40) != 0)
        {
          cliPrintf("%6dus  ?          : 0x%02X 프레임 아님, 1 clk 진행\n",
                    (int)t_us, (int)req);
          i += 1;
          continue;
        }

        ap_ndp = (req >> 1) & 1;
        rd_nwr = (req >> 2) & 1;
        a2     = (req >> 3) & 1;
        a3     = (req >> 4) & 1;
        par    = (req >> 5) & 1;
        park   = (req >> 7) & 1;
        par_exp = (ap_ndp ^ rd_nwr ^ a2 ^ a3) & 1;

        cliPrintf("%6dus  request    : 0x%02X  %s %s A=0x%X  par=%d%s park=%d%s\n",
                  (int)t_us, (int)req,
                  ap_ndp ? "AP" : "DP",
                  rd_nwr ? "RD" : "WR",
                  (int)((a3 << 3) | (a2 << 2)),
                  (int)par, (par == par_exp) ? "" : "(BAD!)",
                  (int)park, park ? "" : "(BAD!)");
        i += 8;

        // turnaround 1클럭 + ACK 3비트
        if ((i + 4) > la_bit_cnt) break;
        i += 1;
        for (int k = 0; k < 3; k++) ack |= ((uint32_t)la_bit[i+k]) << k;
        i += 3;

        cliPrintf("            ack        : %d%d%d = %s\n",
                  (int)(ack & 1), (int)((ack >> 1) & 1), (int)((ack >> 2) & 1),
                  (ack == 1) ? "OK" :
                  (ack == 2) ? "WAIT" :
                  (ack == 4) ? "FAULT" :
                  (ack == 7) ? "NO-RESP (타깃이 드라이브 안함)" : "PROTOCOL");

        if (ack == 1)
        {
          uint32_t data = 0;
          uint32_t dpar;
          uint32_t dpar_exp;

          if (rd_nwr == 0)
          {
            i += 1;                 // write 는 데이터 앞에 turnaround
          }
          if ((i + 33) > la_bit_cnt) break;

          for (int k = 0; k < 32; k++) data |= ((uint32_t)la_bit[i+k]) << k;
          i += 32;
          dpar = la_bit[i];
          i += 1;

          dpar_exp = data;
          dpar_exp ^= dpar_exp >> 16;
          dpar_exp ^= dpar_exp >> 8;
          dpar_exp ^= dpar_exp >> 4;
          dpar_exp ^= dpar_exp >> 2;
          dpar_exp ^= dpar_exp >> 1;
          dpar_exp &= 1;

          cliPrintf("            data       : 0x%08X  par=%d%s\n",
                    data, (int)dpar, (dpar == dpar_exp) ? "" : " (BAD!)");

          if (rd_nwr == 1)
          {
            i += 1;                 // read 는 데이터 뒤에 turnaround
          }
        }
        continue;
      }
    }

    // SWDIO=0 구간은 idle
    run = 0;
    while ((i + run) < la_bit_cnt && la_bit[i + run] == 0) run++;
    if (run > 0)
    {
      cliPrintf("%6dus  idle       : SWDIO=0, %d clk\n", (int)t_us, (int)run);
      i += run;
    }
    else
    {
      i++;
    }
  }
}

#endif

#endif
