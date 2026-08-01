/*
 * swd.c
 *
 *  SWD 전송 계층 (GPIO 비트뱅잉)
 *
 *  핀 소유권
 *    SWCLK/SWDIO 는 gpio_tbl[] 에 등록하지 않고 이 파일이 직접 레지스터로 다룬다.
 *    gpioPinWrite() -> HAL_GPIO_WritePin() 은 호출당 수십 사이클이라 MHz 급에서
 *    쓸 수 없고, SWDIO 의 방향 전환은 HAL_GPIO_Init() 으로는 아예 불가능하다.
 *
 *  SWDIO 방향 전환
 *    MODER 을 read-modify-write 하면 다른 코드가 같은 포트의 핀을 건드릴 때
 *    경합이 난다. 그래서 연결 시점에 MODER 전체 워드 두 벌을 캐시해 두고
 *    단일 32비트 스토어로 전환한다.
 *    => swdConnect() ~ swdDisconnect() 사이에 다른 코드가 SWDIO 포트의 핀을
 *       재설정하면 안 된다. swdIsBusy() 로 가드한다.
 *
 *  비트 타이밍은 ARM CMSIS-DAP 의 DAP_config.h 관례를 그대로 따른다.
 *    쓰기 : SWDIO 세팅 -> SWCLK=0 -> delay -> SWCLK=1 -> delay
 *    읽기 : SWCLK=0 -> delay -> SWDIO 샘플 -> SWCLK=1 -> delay
 *  호스트는 하강 에지 직후 SWDIO 를 바꾸고, 타깃은 상승 에지에서 샘플한다.
 */

#include "swd.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"
#include "swd/swd_algo.h"
#include "swd/swd_la.h"
#include "cli.h"


#ifdef _USE_HW_SWD


/* 핀 배선.
   핀 번호는 GPIO_PIN_x 마스크가 아니라 0~15 의 비트 위치다.
   BSRR/IDR/MODER 을 직접 다루기 때문이다. */
#define SWD_CLK_PORT      GPIOE
#define SWD_CLK_PIN       3
#define SWD_IO_PORT       GPIOC
#define SWD_IO_PIN        10

/* nRST 는 아직 배선하지 않았다. 물릴 때 1 로 바꾸고 아래 핀을 맞춘다.
   swdHasRst()/swdRstSet() API 는 이미 확정되어 있으므로 호출부는 안 바뀐다. */
#define SWD_USE_RST       0
#define SWD_RST_PORT      GPIOE
#define SWD_RST_PIN       2

#define SWD_LINE_RESET_CLK    56      // 스펙은 50 이상
#define SWD_MEAS_CNT          1000    // 속도 실측 토글 횟수
#define SWD_SPEED_KHZ_MAX     100000  // khz*1000 오버플로 방지용 클램프


#define SWCLK_HI()        (SWD_CLK_PORT->BSRR = (1UL << SWD_CLK_PIN))
#define SWCLK_LO()        (SWD_CLK_PORT->BSRR = (1UL << (SWD_CLK_PIN + 16)))
#define SWDIO_HI()        (SWD_IO_PORT->BSRR  = (1UL << SWD_IO_PIN))
#define SWDIO_LO()        (SWD_IO_PORT->BSRR  = (1UL << (SWD_IO_PIN + 16)))
#define SWDIO_WR(b)       (SWD_IO_PORT->BSRR  = (b) ? (1UL << SWD_IO_PIN) : (1UL << (SWD_IO_PIN + 16)))
#define SWDIO_RD()        ((SWD_IO_PORT->IDR >> SWD_IO_PIN) & 1UL)

#define SWDIO_OUT()       (SWD_IO_PORT->MODER = moder_out)
#define SWDIO_IN()        (SWD_IO_PORT->MODER = moder_in)


static bool     is_init      = false;
static bool     is_connected = false;
static volatile bool is_busy = false;

static uint32_t moder_in;
static uint32_t moder_out;

static uint32_t swd_half_cyc;             // 반주기 지연 사이클. 0 이면 최대 속도
static uint32_t swd_loop_ovh = 1;         // 지연 없이 도는 반주기 비용(사이클)
static uint32_t swd_ospeed = 0;           // OSPEEDR. 0=LOW 1=MED 2=HIGH 3=VERY_HIGH
                                          // 점퍼선 실측에서 LOW 만 무오류였다. 아래 주석 참조.
static uint32_t swd_khz_req;
static uint32_t swd_khz_act;
static uint32_t swd_idcode;


#ifdef _USE_HW_CLI
static void cliSwd(cli_args_t *args);
#endif

static void     swdPinInit(void);
static void     swdModerCache(void);
static void     swdBegin(void);
static void     swdCalibrate(void);
static uint32_t swdMeasureRaw(void);

static inline void     swdDelay(void);
static inline void     swdClockCycle(void);
static inline void     swdWriteBit(uint32_t bit);
static inline uint32_t swdReadBit(void);
static inline uint32_t swdParity32(uint32_t value);
static inline uint8_t  swdMakeRequest(uint32_t ap_ndp, uint32_t rd_nwr, uint32_t a2, uint32_t a3);


// ----------------------------------------------------------------- 초기화

bool swdInit(void)
{
  bool ret = true;


  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  swdBegin();
  swdCalibrate();

  swd_khz_req = HW_SWD_SPEED_KHZ;
  swdSetSpeed(swd_khz_req);

  is_init      = true;
  is_connected = false;

  swdLaInit();

#ifdef _USE_HW_CLI
  cliAdd("swd", cliSwd);
#endif

  logPrintf("[%s] swdInit()\n", ret ? "OK" : "NG");
  logPrintf("     clk  : P%c%d\n", 'A' + ((uint32_t)SWD_CLK_PORT - GPIOA_BASE) / 0x400, SWD_CLK_PIN);
  logPrintf("     dio  : P%c%d\n", 'A' + ((uint32_t)SWD_IO_PORT  - GPIOA_BASE) / 0x400, SWD_IO_PIN);
  logPrintf("     max  : %d KHz\n", (int)(SystemCoreClock / (2 * swd_loop_ovh) / 1000));

  return ret;
}

bool swdIsInit(void)
{
  return is_init;
}

bool swdIsBusy(void)
{
  return is_busy;
}


// ----------------------------------------------------------------- 속도

uint32_t swdMeasureSpeed(void)
{
  uint32_t khz;

  // 선점이 끼면 낮게 나온다. 세 번 재서 최댓값을 쓰면 선점된 회차가 걸러진다.
  swd_khz_act = 0;
  for (int i = 0; i < 3; i++)
  {
    khz = swdMeasureRaw();
    if (khz > swd_khz_act)
    {
      swd_khz_act = khz;
    }
  }

  return swd_khz_act;
}

bool swdSetSpeed(uint32_t khz)
{
  uint32_t half;

  if (is_busy == true)
  {
    return false;
  }

  if (khz > SWD_SPEED_KHZ_MAX)
  {
    khz = SWD_SPEED_KHZ_MAX;
  }
  swd_khz_req = khz;

  if (khz == 0)
  {
    swd_half_cyc = 0;
  }
  else
  {
    half = SystemCoreClock / (2 * khz * 1000);
    swd_half_cyc = (half > swd_loop_ovh) ? (half - swd_loop_ovh) : 0;
  }

  swdMeasureSpeed();

  return true;
}

uint32_t swdGetSpeed(void)
{
  return swd_khz_req;
}

uint32_t swdGetSpeedActual(void)
{
  return swd_khz_act;
}


// ----------------------------------------------------------------- 핀 진단

/* 속도 실측은 CPU 루프 시간만 재기 때문에 핀이 전혀 움직이지 않아도 멀쩡한
   주파수를 보고한다. STM32 는 출력 모드에서도 IDR 이 실제 패드 상태를
   반영하므로, 되읽기로 MODER 오류나 GND 단락을 0 비용으로 잡을 수 있다. */
bool swdPinTest(void)
{
  bool ret = true;

  swdBegin();

  SWCLK_HI();
  delayUs(1);
  if (((SWD_CLK_PORT->IDR >> SWD_CLK_PIN) & 1UL) != 1UL) ret = false;

  SWCLK_LO();
  delayUs(1);
  if (((SWD_CLK_PORT->IDR >> SWD_CLK_PIN) & 1UL) != 0UL) ret = false;

  SWCLK_HI();

  SWDIO_OUT();
  SWDIO_HI();
  delayUs(1);
  if (SWDIO_RD() != 1UL) ret = false;

  SWDIO_LO();
  delayUs(1);
  if (SWDIO_RD() != 0UL) ret = false;

  SWDIO_HI();

  return ret;
}


// ----------------------------------------------------------------- 시퀀스

void swdClock(uint32_t count)
{
  while (count--)
  {
    swdClockCycle();
  }
}

void swdIdle(uint32_t count)
{
  SWDIO_OUT();
  SWDIO_LO();
  swdClock(count);
  SWDIO_HI();
}

void swdLineReset(void)
{
  SWDIO_OUT();
  SWDIO_HI();
  swdClock(SWD_LINE_RESET_CLK);
}

/* JTAG -> SWD 전환.
   line reset -> 매직 0xE79E (16비트 LSB first) -> line reset -> idle
   전환 후에는 반드시 DPIDR 을 한 번 읽어야 DP 가 reset state 를 벗어난다. */
void swdJtagToSwd(void)
{
  swdLineReset();

  SWDIO_OUT();
  for (int i = 0; i < 16; i++)
  {
    swdWriteBit(0xE79EUL >> i);
  }

  swdLineReset();
  swdIdle(8);
}

void swdSelectDormantExit(void)
{
  // ADIv5.2 이상 파트용. STM32F4 같은 DPv1 은 필요 없다.
  // 나중에 선택 alert 128비트 + 활성화 코드 0x1A 를 여기에 넣는다.
}


// ----------------------------------------------------------------- 전송

uint32_t swdTransfer(uint8_t ap_ndp, uint8_t rd_nwr, uint8_t addr, uint32_t *p_data)
{
  uint8_t  req;
  uint32_t ack = 0;
  uint32_t value;
  uint32_t parity;

  req = swdMakeRequest(ap_ndp & 1, rd_nwr & 1, (addr >> 2) & 1, (addr >> 3) & 1);

  is_busy = true;

  // Request 8비트
  SWDIO_OUT();
  for (int i = 0; i < 8; i++)
  {
    swdWriteBit(req >> i);
  }

  // Turnaround 1클럭 -> ACK 3비트 (타깃이 드라이브)
  SWDIO_IN();
  swdClockCycle();
  for (int i = 0; i < 3; i++)
  {
    ack |= swdReadBit() << i;
  }

  if (ack == SWD_ACK_OK)
  {
    if (rd_nwr)
    {
      // READ: 데이터 32비트 + 패리티를 타깃이 드라이브한 뒤 turnaround
      value = 0;
      for (int i = 0; i < 32; i++)
      {
        value |= swdReadBit() << i;
      }
      parity = swdReadBit();

      swdClockCycle();
      SWDIO_HI();
      SWDIO_OUT();

      if (parity != swdParity32(value))
      {
        ack = SWD_ACK_PARITY;
      }
      if (p_data != NULL)
      {
        *p_data = value;
      }
    }
    else
    {
      // WRITE: turnaround 뒤에 호스트가 데이터 32비트 + 패리티를 구동
      swdClockCycle();
      SWDIO_OUT();

      value = (p_data != NULL) ? *p_data : 0;
      for (int i = 0; i < 32; i++)
      {
        swdWriteBit(value >> i);
      }
      swdWriteBit(swdParity32(value));
    }

    // 트레일링 idle. 매 패킷 뒤에 넣지 않으면 다음 트랜잭션이 간헐적으로 깨진다.
    SWDIO_LO();
    swdClock(HW_SWD_IDLE_CYCLES);
    SWDIO_HI();
  }
  else if (ack == SWD_ACK_WAIT || ack == SWD_ACK_FAULT)
  {
    // ORUNDETECT=0 이므로 WAIT/FAULT 뒤에는 데이터 페이즈가 없다
    swdClockCycle();
    SWDIO_HI();
    SWDIO_OUT();
  }
  else
  {
    // 프로토콜 에러 또는 무응답. 타깃이 데이터 페이즈를 구동 중일 수 있으므로
    // turnaround + 32 + 1 클럭을 흘려보내 라인을 정리한다.
    swdClock(1 + 32 + 1);
    SWDIO_HI();
    SWDIO_OUT();
  }

  is_busy = false;

  // 실패한 트랜잭션을 그대로 보존한다. 간헐적 실패는 이렇게만 잡힌다.
  if (ack != SWD_ACK_OK)
  {
    swdLaAutoTrig();
  }

  return ack;
}

const char *swdAckStr(uint32_t ack)
{
  switch(ack)
  {
    case SWD_ACK_OK:      return "OK";
    case SWD_ACK_WAIT:    return "WAIT";
    case SWD_ACK_FAULT:   return "FAULT";
    case SWD_ACK_NORESP:  return "NO-RESP";
    case SWD_ACK_PARITY:  return "PARITY";
    default:              return "PROTOCOL";
  }
}


// ----------------------------------------------------------------- 링크

swd_err_t swdConnect(uint32_t *p_idcode)
{
  uint32_t ack = 0;
  uint32_t id  = 0;

  is_connected = false;
  swdDapInvalidate();       // 링크가 바뀌면 SELECT/CSW 캐시와 파워업 상태를 버린다

  if (swdPinTest() == false)
  {
    return SWD_ERR_PIN;
  }

  /* 재시도할 때는 line reset 만으로 부족하다. DP 가 아직 JTAG 모드면
     전환 시퀀스를 다시 보내야 한다. */
  for (int retry = 0; retry < 3; retry++)
  {
    swdSelectDormantExit();
    swdJtagToSwd();

    ack = swdTransfer(0, 1, SWD_DP_DPIDR, &id);

    if (ack == SWD_ACK_OK)
    {
      swd_idcode   = id;
      is_connected = true;
      if (p_idcode != NULL)
      {
        *p_idcode = id;
      }
      return SWD_OK;
    }
  }

  if (p_idcode != NULL)
  {
    *p_idcode = id;
  }

  switch(ack)
  {
    case SWD_ACK_WAIT:    return SWD_ERR_WAIT;
    case SWD_ACK_FAULT:   return SWD_ERR_FAULT;
    case SWD_ACK_PARITY:  return SWD_ERR_PARITY;
    case SWD_ACK_NORESP:  return SWD_ERR_NORESP;
    default:              return SWD_ERR_PROTOCOL;
  }
}

/* 연결이 안 되면 속도를 절반씩 낮추며 재시도한다.
   배선이 길거나 타깃 전원이 약할 때 쓸 만한 속도를 자동으로 찾아준다. */
swd_err_t swdConnectAuto(uint32_t *p_idcode, uint32_t *p_khz)
{
  const uint32_t khz_tbl[] = {4000, 2000, 1000, 500, 250, 100, 50};
  swd_err_t err = SWD_ERR_NORESP;

  for (uint32_t i = 0; i < sizeof(khz_tbl)/sizeof(khz_tbl[0]); i++)
  {
    swdSetSpeed(khz_tbl[i]);

    err = swdConnect(p_idcode);
    if (err == SWD_OK)
    {
      if (p_khz != NULL)
      {
        *p_khz = khz_tbl[i];
      }
      return SWD_OK;
    }
    if (err == SWD_ERR_PIN)
    {
      break;
    }
  }

  return err;
}

void swdDisconnect(void)
{
  swdIdle(8);
  is_connected = false;
  swdDapInvalidate();
}

bool swdIsConnected(void)
{
  return is_connected;
}


// ----------------------------------------------------------------- 핀 정보

volatile uint32_t *swdGetClkIdr(void)
{
  return &SWD_CLK_PORT->IDR;
}

volatile uint32_t *swdGetIoIdr(void)
{
  return &SWD_IO_PORT->IDR;
}

uint8_t swdGetClkPin(void)
{
  return SWD_CLK_PIN;
}

uint8_t swdGetIoPin(void)
{
  return SWD_IO_PIN;
}


// ----------------------------------------------------------------- nRST

bool swdHasRst(void)
{
#if SWD_USE_RST
  return true;
#else
  return false;
#endif
}

void swdRstSet(bool level)
{
#if SWD_USE_RST
  if (level)
    SWD_RST_PORT->BSRR = (1UL << SWD_RST_PIN);
  else
    SWD_RST_PORT->BSRR = (1UL << (SWD_RST_PIN + 16));
#else
  (void)level;
#endif
}


// ----------------------------------------------------------------- 내부

static inline void swdDelay(void)
{
  uint32_t n = swd_half_cyc;

  if (n)
  {
    uint32_t t_start = DWT->CYCCNT;

    while ((DWT->CYCCNT - t_start) < n)
    {
    }
  }
}

static inline void swdClockCycle(void)
{
  SWCLK_LO();
  swdDelay();
  SWCLK_HI();
  swdDelay();
}

static inline void swdWriteBit(uint32_t bit)
{
  SWDIO_WR(bit & 1UL);
  SWCLK_LO();
  swdDelay();
  SWCLK_HI();
  swdDelay();
}

static inline uint32_t swdReadBit(void)
{
  uint32_t bit;

  SWCLK_LO();
  swdDelay();
  bit = SWDIO_RD();
  SWCLK_HI();
  swdDelay();

  return bit;
}

static inline uint32_t swdParity32(uint32_t value)
{
  value ^= value >> 16;
  value ^= value >> 8;
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;

  return value & 1UL;
}

/* Request 바이트는 LSB first 로 나간다.
     bit0 Start=1, bit1 APnDP, bit2 RnW, bit3 A2, bit4 A3,
     bit5 Parity, bit6 Stop=0, bit7 Park=1
   DPIDR 읽기(APnDP=0, RnW=1, A=0x0) 는 0xA5 가 되어야 한다. */
static inline uint8_t swdMakeRequest(uint32_t ap_ndp, uint32_t rd_nwr, uint32_t a2, uint32_t a3)
{
  uint32_t parity = (ap_ndp ^ rd_nwr ^ a2 ^ a3) & 1UL;

  return (uint8_t)(0x81UL
                   | (ap_ndp << 1)
                   | (rd_nwr << 2)
                   | (a2     << 3)
                   | (a3     << 4)
                   | (parity << 5));
}

/* SWCLK 는 푸시풀 출력, SWDIO 는 푸시풀 + 약한 풀업(타깃 유휴 레벨과 맞춘다).
 *
 * OSPEEDR(드라이브 강도)이 중요하다. 높일수록 좋을 것 같지만 반대다.
 * 종단 없는 점퍼선에서는 VERY_HIGH 의 서브나노초 에지가 링잉과 반사를 만들어
 * 비트 오류를 낸다. 이 오류는 클럭 주기가 아니라 에지 기울기에 의존하므로
 * 속도를 낮춰도 사라지지 않는 게 특징이고, 그래서 원인을 오해하기 쉽다.
 *
 * STM32F411 타깃 + 점퍼선 실측 (32워드 블록 읽기 20회):
 *   LOW 0 실패 / MEDIUM 3 / HIGH 11 / VERY_HIGH 9
 *
 * LOW 로 고정한 뒤 대량 전송(32KB, 약 8200 트랜잭션) 5회 반복 실측:
 *   1.8MHz 5/5   3.5MHz 5/5   5.7MHz 3/5   23MHz 2/5
 * 짧은 전송은 최대 속도로도 통과하므로 작은 표본에 속기 쉽다. 트랜잭션당
 * 오류율이 낮아도 수천 번 반복하면 드러난다. 이 배선의 실용 상한은 ~3.5MHz.
 *
 * 제대로 하려면 SWCLK/SWDIO 에 직렬 22~33옴, SWDIO 에 풀업 10k, 짧은 GND
 * 리턴을 두는 게 맞다. 배선이 개선되면 swd drv 로 올려서 다시 재면 된다. */
void swdPinInit(void)
{
  uint32_t pos;

  pos = SWD_CLK_PIN * 2;
  SWD_CLK_PORT->OSPEEDR = (SWD_CLK_PORT->OSPEEDR & ~(3UL << pos)) | (swd_ospeed << pos);
  SWD_CLK_PORT->PUPDR   = (SWD_CLK_PORT->PUPDR   & ~(3UL << pos));
  SWD_CLK_PORT->OTYPER &= ~(1UL << SWD_CLK_PIN);
  SWD_CLK_PORT->BSRR    = (1UL << SWD_CLK_PIN);                     // 유휴 시 high
  SWD_CLK_PORT->MODER   = (SWD_CLK_PORT->MODER   & ~(3UL << pos)) | (1UL << pos);

  pos = SWD_IO_PIN * 2;
  SWD_IO_PORT->OSPEEDR  = (SWD_IO_PORT->OSPEEDR  & ~(3UL << pos)) | (swd_ospeed << pos);
  SWD_IO_PORT->PUPDR    = (SWD_IO_PORT->PUPDR    & ~(3UL << pos)) | (1UL << pos);
  SWD_IO_PORT->OTYPER  &= ~(1UL << SWD_IO_PIN);
  SWD_IO_PORT->BSRR     = (1UL << SWD_IO_PIN);
  SWD_IO_PORT->MODER    = (SWD_IO_PORT->MODER    & ~(3UL << pos)) | (1UL << pos);

#if SWD_USE_RST
  pos = SWD_RST_PIN * 2;
  SWD_RST_PORT->OTYPER |= (1UL << SWD_RST_PIN);               // 오픈드레인
  SWD_RST_PORT->PUPDR   = (SWD_RST_PORT->PUPDR & ~(3UL << pos)) | (1UL << pos);
  SWD_RST_PORT->BSRR    = (1UL << SWD_RST_PIN);
  SWD_RST_PORT->MODER   = (SWD_RST_PORT->MODER & ~(3UL << pos)) | (1UL << pos);
#endif
}

/* MODER 전체 워드를 미리 만들어 두면 방향 전환이 단일 스토어로 끝난다.
   read-modify-write 가 아니므로 인터럽트 마스킹도 필요 없다. */
void swdModerCache(void)
{
  uint32_t pos = SWD_IO_PIN * 2;
  uint32_t m   = SWD_IO_PORT->MODER & ~(3UL << pos);

  moder_in  = m;                    // 00 = input
  moder_out = m | (1UL << pos);     // 01 = output
}

/* 유휴 상태에서 SWD 를 쓰기 직전에 부른다.
 *
 * MODER 캐시를 반드시 여기서 갱신해야 한다. swdInit() 은 hwInit() 안에서
 * gpioInit() 직후에 도는데, 같은 포트를 쓰는 spiInit()/sdInit()/qspiInit()/
 * ltdcInit() 은 그 뒤에 돈다. init 때 캐시한 워드를 그대로 쓰면 SWDIO_OUT()
 * 한 번에 PC1(SDMMC2_CK), PC6/7/9(LTDC), PC11(QSPI) 이 전부 초기화 이전
 * 상태로 되돌아가서 SD/LCD/QSPI 가 죽는다.
 *
 * 갱신한 뒤부터 swdDisconnect() 까지는 다른 코드가 이 포트의 핀을 재설정하면
 * 안 된다. sdUpdate() 의 카드 삽입 경로가 sdReInit() 을 부르는 게 대표적이다.
 */
void swdBegin(void)
{
  swdPinInit();
  swdModerCache();
}

/* 지연 없이 돌 때의 반주기 비용을 실측해서 역산한다. 하드코딩하면 컴파일
   옵션이나 캐시 상태가 바뀔 때 어긋난다. */
void swdCalibrate(void)
{
  uint32_t khz_max;

  swd_half_cyc = 0;
  khz_max = swdMeasureRaw();

  if (khz_max > 0)
  {
    swd_loop_ovh = SystemCoreClock / (2 * khz_max * 1000);
  }
  if (swd_loop_ovh == 0)
  {
    swd_loop_ovh = 1;
  }
}

/* SWDIO=0 으로 SWCLK 를 N회 토글하고 micros() 로 브래킷한다.
   간이 측정이지만 해상도는 충분하다. 1MHz 에서 1000토글이면 1000us 라 0.1%,
   최대 속도 8MHz 에서도 125us 라 0.8% 다.
   SWDIO 를 low 로 두는 건 이게 SWD 유휴 구간이라 타깃을 건드리지 않기 때문이다.
   (high 로 50클럭 이상 보내면 그건 line reset 이 된다) */
uint32_t swdMeasureRaw(void)
{
  uint32_t t_start;
  uint32_t t_us;

  swdBegin();
  SWDIO_OUT();
  SWDIO_LO();

  t_start = micros();
  for (int i = 0; i < SWD_MEAS_CNT; i++)
  {
    SWCLK_LO();
    swdDelay();
    SWCLK_HI();
    swdDelay();
  }
  t_us = micros() - t_start;

  SWCLK_HI();

  if (t_us == 0)
  {
    return 0;
  }
  return (SWD_MEAS_CNT * 1000UL) / t_us;
}


// ----------------------------------------------------------------- CLI

#ifdef _USE_HW_CLI
static void cliSwdPrintId(uint32_t id)
{
  cliPrintf("DPIDR    : 0x%08X\n", id);
  cliPrintf("  REVISION : 0x%X\n", (id >> 28) & 0x0F);
  cliPrintf("  PARTNO   : 0x%02X\n", (id >> 20) & 0xFF);
  cliPrintf("  MIN      : %d\n", (id >> 16) & 0x01);
  cliPrintf("  VERSION  : %d (DPv%d)\n", (id >> 12) & 0x0F, (id >> 12) & 0x0F);
  cliPrintf("  DESIGNER : 0x%03X %s\n", (id >> 1) & 0x7FF,
            (((id >> 1) & 0x7FF) == 0x23B) ? "(ARM)" : "");
}

void cliSwd(cli_args_t *args)
{
  bool ret = false;

  // 핀을 직접 찌르는 명령들(id/clk/pin/reset)도 있으므로 진입 시 한 번 갱신한다.
  swdBegin();


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("swd init : %s\n", is_init ? "OK" : "NG");
    cliPrintf("clk pin  : P%c%d\n", 'A' + ((uint32_t)SWD_CLK_PORT - GPIOA_BASE)/0x400, SWD_CLK_PIN);
    cliPrintf("dio pin  : P%c%d\n", 'A' + ((uint32_t)SWD_IO_PORT  - GPIOA_BASE)/0x400, SWD_IO_PIN);
    cliPrintf("nrst     : %s\n", swdHasRst() ? "wired" : "none (sw reset only)");
    {
      const char *nm[4] = {"LOW", "MEDIUM", "HIGH", "VERY_HIGH"};
      cliPrintf("drive    : %d (%s)\n", (int)swd_ospeed, nm[swd_ospeed & 3]);
    }
    cliPrintf("speed    : req %d KHz, act %d KHz\n", (int)swd_khz_req, (int)swd_khz_act);
    cliPrintf("half cyc : %d (ovh %d)\n", (int)swd_half_cyc, (int)swd_loop_ovh);
    cliPrintf("cpu clk  : %d MHz\n", (int)(SystemCoreClock/1000000));
    cliPrintf("connect  : %s\n", is_connected ? "yes" : "no");
    if (is_connected)
    {
      cliPrintf("idcode   : 0x%08X\n", swd_idcode);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "drv") == true)
  {
    const char *nm[4] = {"LOW", "MEDIUM", "HIGH", "VERY_HIGH"};

    cliPrintf("drive    : %d (%s)\n", (int)swd_ospeed, nm[swd_ospeed & 3]);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "drv") == true)
  {
    const char *nm[4] = {"LOW", "MEDIUM", "HIGH", "VERY_HIGH"};

    swd_ospeed = (uint32_t)args->getData(1) & 3;
    swdBegin();
    cliPrintf("drive    : %d (%s)\n", (int)swd_ospeed, nm[swd_ospeed]);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "pintest") == true)
  {
    bool ok = swdPinTest();

    cliPrintf("pin test : %s\n", ok ? "OK" : "NG");
    if (ok == false)
    {
      cliPrintf("  핀이 토글되지 않는다. MODER/OSPEEDR 설정, 배선, GND 단락 확인\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "speed") == true)
  {
    swdMeasureSpeed();
    cliPrintf("speed    : req %d KHz, act %d KHz\n", (int)swd_khz_req, (int)swd_khz_act);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "speed") == true)
  {
    uint32_t khz = (uint32_t)args->getData(1);

    if (swdSetSpeed(khz) == true)
    {
      cliPrintf("speed    : req %d KHz, act %d KHz\n", (int)swd_khz_req, (int)swd_khz_act);
    }
    else
    {
      cliPrintf("busy\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "connect") == true)
  {
    uint32_t  id = 0;
    swd_err_t err;

    err = swdConnect(&id);
    if (err == SWD_OK)
    {
      cliSwdPrintId(id);
      cliPrintf("connect  : OK (%d KHz)\n", (int)swd_khz_act);
    }
    else
    {
      cliPrintf("connect  : FAIL (err %d)\n", err);
      cliPrintf("  raw    : 0x%08X\n", id);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "connect") == true && args->isStr(1, "auto") == true)
  {
    uint32_t  id  = 0;
    uint32_t  khz = 0;
    swd_err_t err;

    err = swdConnectAuto(&id, &khz);
    if (err == SWD_OK)
    {
      cliSwdPrintId(id);
      cliPrintf("connect  : OK at %d KHz\n", (int)khz);
    }
    else
    {
      cliPrintf("connect  : FAIL (err %d)\n", err);
    }
    ret = true;
  }

  // 타깃 전원을 다시 넣는 순간을 잡기 위한 반복 시도.
  // nRST 가 없으면 SWD 를 끄는 펌웨어가 든 타깃은 이 방법뿐이다.
  if (args->argc == 2 && args->isStr(0, "connect") == true && args->isStr(1, "loop") == true)
  {
    uint32_t id  = 0;
    uint32_t cnt = 0;

    while(cliKeepLoop())
    {
      cnt++;
      if (swdConnect(&id) == SWD_OK)
      {
        cliPrintf("\n[%d] connect OK  DPIDR 0x%08X\n", (int)cnt, id);
        break;
      }
      if ((cnt % 50) == 0)
      {
        cliPrintf(".");
      }
      delay(10);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "id") == true)
  {
    uint32_t id  = 0;
    uint32_t ack = swdTransfer(0, 1, SWD_DP_DPIDR, &id);

    cliPrintf("ack      : %s\n", swdAckStr(ack));
    if (ack == SWD_ACK_OK)
    {
      cliSwdPrintId(id);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset") == true)
  {
    swdLineReset();
    swdIdle(8);
    cliPrintf("line reset (%d clk)\n", SWD_LINE_RESET_CLK);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "clk") == true)
  {
    uint32_t cnt = (uint32_t)args->getData(1);

    SWDIO_OUT();
    SWDIO_HI();
    swdClock(cnt);
    cliPrintf("clk %d\n", (int)cnt);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "pin") == true)
  {
    uint8_t level = (uint8_t)args->getData(2);

    if (args->isStr(1, "clk") == true)
    {
      if (level) SWCLK_HI(); else SWCLK_LO();
      cliPrintf("swclk = %d\n", level);
      ret = true;
    }
    if (args->isStr(1, "dio") == true)
    {
      SWDIO_OUT();
      SWDIO_WR(level);
      cliPrintf("swdio = %d\n", level);
      ret = true;
    }
  }

  if (args->argc == 1 && args->isStr(0, "pinread") == true)
  {
    SWDIO_IN();
    delayUs(10);
    cliPrintf("swclk = %d (out)\n", (int)((SWD_CLK_PORT->IDR >> SWD_CLK_PIN) & 1));
    cliPrintf("swdio = %d (in, 풀업이면 1)\n", (int)SWDIO_RD());
    SWDIO_OUT();
    ret = true;
  }

  // 어느 속도까지 링크가 유지되는지 한 번에 본다. Stage 1 의 핵심 진단.
  if (args->argc == 1 && args->isStr(0, "scan") == true)
  {
    const uint32_t khz_tbl[] = {50, 100, 250, 500, 1000, 2000, 4000, 0};
    uint32_t khz_bak = swd_khz_req;

    for (uint32_t i = 0; i < sizeof(khz_tbl)/sizeof(khz_tbl[0]); i++)
    {
      uint32_t  id = 0;
      swd_err_t err;

      swdSetSpeed(khz_tbl[i]);
      err = swdConnect(&id);

      cliPrintf("%6d KHz (act %6d) : 0x%08X  %s\n",
                (int)khz_tbl[i], (int)swd_khz_act, id,
                (err == SWD_OK) ? "OK" : "FAIL");
    }
    swdSetSpeed(khz_bak);
    ret = true;
  }

  // ---- DP/AP, 타깃 메모리 ---------------------------------------------

  if (args->argc == 1 && args->isStr(0, "power") == true)
  {
    swd_err_t err = swdDapEnsure();
    uint32_t  ctrl = 0;

    swdDpRead(SWD_DP_CTRL_STAT, &ctrl);
    cliPrintf("power    : %s\n", swdErrStr(err));
    cliPrintf("CTRL/STAT: 0x%08X\n", ctrl);
    cliPrintf("  CSYSPWRUPACK %d  CDBGPWRUPACK %d\n",
              (int)((ctrl >> 31) & 1), (int)((ctrl >> 29) & 1));
    cliPrintf("  STICKYERR %d  STICKYORUN %d  WDATAERR %d\n",
              (int)((ctrl >> 5) & 1), (int)((ctrl >> 1) & 1), (int)((ctrl >> 7) & 1));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "stat") == true)
  {
    uint32_t e=0, ro=0, rn=0, to=0, tn=0;

    swdDapGetStat(&e, &ro, &rn, &to, &tn);
    cliPrintf("link err   : %d\n", (int)e);
    cliPrintf("recover    : ok %d / fail %d\n", (int)ro, (int)rn);
    cliPrintf("chunk retry: ok %d / 최종실패 %d\n", (int)to, (int)tn);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "stat") == true && args->isStr(1, "clear") == true)
  {
    swdDapClearStat();
    cliPrintf("stat cleared\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "apid") == true)
  {
    uint32_t idr = 0, cfg = 0, base = 0;

    if (swdDapEnsure() == SWD_OK)
    {
      swdApRead(SWD_AP_IDR,  &idr);
      swdApRead(SWD_AP_CFG,  &cfg);
      swdApRead(SWD_AP_BASE, &base);

      cliPrintf("AP%d IDR  : 0x%08X %s\n", swdDapGetAp(), idr,
                (idr == 0x24770011) ? "(ARM AHB-AP, Cortex-M3/M4)" : "");
      cliPrintf("AP%d CFG  : 0x%08X\n", swdDapGetAp(), cfg);
      cliPrintf("AP%d BASE : 0x%08X %s\n", swdDapGetAp(), base,
                (base & 1) ? "(ROM table present)" : "(no ROM table)");
    }
    else
    {
      cliPrintf("connect fail\n");
    }
    ret = true;
  }

  // AP 열거. 멀티코어(H7 듀얼코어 등) 대비로 지금 넣어 둔다.
  if (args->argc == 1 && args->isStr(0, "apscan") == true)
  {
    uint8_t ap_bak = swdDapGetAp();

    if (swdDapEnsure() == SWD_OK)
    {
      for (int i = 0; i < 8; i++)
      {
        uint32_t idr = 0;

        swdDapSetAp((uint8_t)i);
        if (swdApRead(SWD_AP_IDR, &idr) == SWD_OK && idr != 0)
        {
          cliPrintf("AP%d : 0x%08X\n", i, idr);
        }
      }
    }
    swdDapSetAp(ap_bak);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "ap") == true)
  {
    swdDapSetAp((uint8_t)args->getData(1));
    cliPrintf("ap sel : %d\n", swdDapGetAp());
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "dpread") == true)
  {
    uint32_t  addr = (uint32_t)args->getData(1);
    uint32_t  data = 0;
    swd_err_t err;

    swdDapEnsure();
    err = swdDpRead((uint8_t)addr, &data);
    cliPrintf("DP[0x%X] : 0x%08X  %s\n", addr, data, swdErrStr(err));
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "dpwrite") == true)
  {
    uint32_t  addr = (uint32_t)args->getData(1);
    uint32_t  data = (uint32_t)args->getData(2);
    swd_err_t err;

    swdDapEnsure();
    err = swdDpWrite((uint8_t)addr, data);
    cliPrintf("DP[0x%X] <= 0x%08X  %s\n", addr, data, swdErrStr(err));
    ret = true;
  }

  // 타깃 메모리 덤프. 호스트 메모리 덤프인 "md" 와 이름이 겹치지 않게 swd md.
  if (args->argc >= 2 && args->isStr(0, "md") == true)
  {
    uint32_t addr  = (uint32_t)args->getData(1);
    uint32_t count = (args->argc == 3) ? (uint32_t)args->getData(2) : 4;
    static uint32_t buf[64];

    while (count > 0)
    {
      uint32_t n = (count > 64) ? 64 : count;
      swd_err_t err = swdMemReadBlock(addr, buf, n);

      if (err != SWD_OK)
      {
        cliPrintf("%08X : %s\n", addr, swdErrStr(err));
        break;
      }

      for (uint32_t i = 0; i < n; i += 4)
      {
        cliPrintf("%08X :", addr + i*4);
        for (uint32_t k = 0; k < 4 && (i+k) < n; k++)
        {
          cliPrintf(" %08X", buf[i+k]);
        }
        cliPrintf("\n");
      }
      addr  += n * 4;
      count -= n;
    }
    ret = true;
  }

  if (args->argc >= 3 && args->isStr(0, "mw") == true)
  {
    uint32_t  addr  = (uint32_t)args->getData(1);
    uint32_t  data  = (uint32_t)args->getData(2);
    uint32_t  count = (args->argc == 4) ? (uint32_t)args->getData(3) : 1;
    swd_err_t err;

    err = swdMemFill(addr, data, count);
    cliPrintf("%08X <= 0x%08X x%d  %s\n", addr, data, (int)count, swdErrStr(err));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "mb") == true)
  {
    uint32_t  addr = (uint32_t)args->getData(1);
    uint8_t   data = 0;
    swd_err_t err  = swdMemRead8(addr, &data);

    cliPrintf("%08X : 0x%02X  %s\n", addr, data, swdErrStr(err));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "mh") == true)
  {
    uint32_t  addr = (uint32_t)args->getData(1);
    uint16_t  data = 0;
    swd_err_t err  = swdMemRead16(addr, &data);

    cliPrintf("%08X : 0x%04X  %s\n", addr, data, swdErrStr(err));
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "wb") == true)
  {
    uint32_t  addr = (uint32_t)args->getData(1);
    uint8_t   data = (uint8_t)args->getData(2);
    swd_err_t err  = swdMemWrite8(addr, data);

    cliPrintf("%08X <= 0x%02X  %s\n", addr, data, swdErrStr(err));
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "wh") == true)
  {
    uint32_t  addr = (uint32_t)args->getData(1);
    uint16_t  data = (uint16_t)args->getData(2);
    swd_err_t err  = swdMemWrite16(addr, data);

    cliPrintf("%08X <= 0x%04X  %s\n", addr, data, swdErrStr(err));
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "bench") == true)
  {
    uint32_t addr = (uint32_t)args->getData(1);
    uint32_t kb   = (uint32_t)args->getData(2);
    static uint32_t buf[256];
    uint32_t left = kb * 256;
    uint32_t t_start;
    uint32_t t_ms;
    swd_err_t err = SWD_OK;

    t_start = millis();
    while (left > 0 && err == SWD_OK)
    {
      uint32_t n = (left > 256) ? 256 : left;

      err = swdMemReadBlock(addr, buf, n);
      addr += n * 4;
      left -= n;
    }
    t_ms = millis() - t_start;

    if (err != SWD_OK)
    {
      cliPrintf("bench fail : %s\n", swdErrStr(err));
    }
    else
    {
      cliPrintf("read %d KB in %d ms -> %d KB/s @ %d KHz\n",
                (int)kb, (int)t_ms, (int)(t_ms ? (kb * 1000 / t_ms) : 0),
                (int)swdGetSpeedActual());
    }
    ret = true;
  }

  // 1KB TAR 랩 검증. 경계를 걸치는 블록을 쓰고 다시 읽어 비교한다.
  // 이 버그는 "1KB 마다 조용히 깨짐" 이라 일반 테스트로는 안 잡힌다.
  if (args->argc == 2 && args->isStr(0, "tartest") == true)
  {
    uint32_t base = (uint32_t)args->getData(1);
    static uint32_t wr[768];
    static uint32_t rd[768];
    swd_err_t err;
    uint32_t  bad = 0;

    // 1KB 경계를 확실히 걸치도록 512B 앞에서 시작해 3KB 를 쓴다
    base = (base + 0x3FF) & ~0x3FFUL;
    base -= 512;

    for (uint32_t i = 0; i < 768; i++) wr[i] = 0xA5000000 | i;

    err = swdMemWriteBlock(base, wr, 768);
    if (err == SWD_OK) err = swdMemReadBlock(base, rd, 768);

    if (err != SWD_OK)
    {
      cliPrintf("tartest : %s\n", swdErrStr(err));
    }
    else
    {
      for (uint32_t i = 0; i < 768; i++)
      {
        if (rd[i] != wr[i])
        {
          if (bad < 4)
          {
            cliPrintf("  %08X : exp %08X got %08X\n", base + i*4, wr[i], rd[i]);
          }
          bad++;
        }
      }
      cliPrintf("tartest : %08X ~ %08X (%d word), mismatch %d -> %s\n",
                base, base + 768*4 - 4, 768, (int)bad, bad ? "FAIL" : "PASS");
    }
    ret = true;
  }

  // ---- Cortex-M 디버그 코어 -------------------------------------------

  if (args->argc == 1 && (args->isStr(0, "halt") || args->isStr(0, "run") ||
                          args->isStr(0, "step") || args->isStr(0, "detach")))
  {
    swd_err_t err;
    const char *what;

    if (args->isStr(0, "halt"))        { err = swdCmHalt();   what = "halt"; }
    else if (args->isStr(0, "run"))    { err = swdCmRun();    what = "run"; }
    else if (args->isStr(0, "step"))   { err = swdCmStep();   what = "step"; }
    else                               { err = swdCmDetach(); what = "detach"; }

    cliPrintf("%-8s : %s\n", what, swdErrStr(err));

    if (err == SWD_OK && !args->isStr(0, "detach"))
    {
      uint32_t dhcsr = 0, dfsr = 0, pc = 0;

      swdCmGetDhcsr(&dhcsr);
      swdMemRead32(CM_DFSR, &dfsr);
      cliPrintf("DHCSR    : 0x%08X  %s%s%s\n", dhcsr,
                (dhcsr & CM_S_HALT)   ? "S_HALT " : "",
                (dhcsr & CM_S_SLEEP)  ? "S_SLEEP " : "",
                (dhcsr & CM_S_LOCKUP) ? "S_LOCKUP " : "");
      cliPrintf("DFSR     : 0x%08X  %s\n", dfsr, swdCmDfsrStr(dfsr));
      if (dhcsr & CM_S_HALT)
      {
        swdCmRegRead(CM_REG_PC, &pc);
        cliPrintf("PC       : 0x%08X\n", pc);
      }
    }
    ret = true;
  }

  if (args->argc == 1 && (args->isStr(0, "rsthalt") || args->isStr(0, "sysreset")))
  {
    bool      is_halt = args->isStr(0, "rsthalt");
    swd_err_t err     = is_halt ? swdCmResetHalt() : swdCmSysReset();

    cliPrintf("%-8s : %s\n", is_halt ? "rsthalt" : "sysreset", swdErrStr(err));

    if (err == SWD_OK && is_halt)
    {
      uint32_t dfsr = swdCmGetLastDfsr();
      uint32_t pc = 0, sp = 0;

      swdCmRegRead(CM_REG_PC, &pc);
      swdCmRegRead(CM_REG_SP, &sp);
      cliPrintf("halt at  : PC 0x%08X  SP 0x%08X\n", pc, sp);
      cliPrintf("reason   : %s\n", swdCmDfsrStr(dfsr));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "regs") == true)
  {
    static const char *nm[] = {"R0 ","R1 ","R2 ","R3 ","R4 ","R5 ","R6 ","R7 ",
                               "R8 ","R9 ","R10","R11","R12","SP ","LR ","PC "};
    uint32_t v[16];
    uint32_t xpsr = 0, msp = 0, psp = 0, ctrl = 0;
    bool halted = false;

    swdCmIsHalted(&halted);
    if (halted == false)
    {
      cliPrintf("코어가 halt 상태가 아니다. swd halt 먼저.\n");
      ret = true;
    }
    else
    {
      for (int i = 0; i < 16; i++)
      {
        if (swdCmRegRead((uint8_t)i, &v[i]) != SWD_OK) v[i] = 0xDEADBEEF;
      }
      swdCmRegRead(CM_REG_XPSR, &xpsr);
      swdCmRegRead(CM_REG_MSP,  &msp);
      swdCmRegRead(CM_REG_PSP,  &psp);
      swdCmRegRead(CM_REG_CTRL, &ctrl);

      for (int i = 0; i < 16; i += 4)
      {
        cliPrintf("%s=%08X %s=%08X %s=%08X %s=%08X\n",
                  nm[i],v[i], nm[i+1],v[i+1], nm[i+2],v[i+2], nm[i+3],v[i+3]);
      }
      cliPrintf("xPSR=%08X (T=%d)  MSP=%08X  PSP=%08X  CTRL=%08X\n",
                xpsr, (int)((xpsr >> 24) & 1), msp, psp, ctrl);
      ret = true;
    }
  }

  if (args->argc >= 2 && args->isStr(0, "reg") == true)
  {
    uint8_t   n = (uint8_t)args->getData(1);
    swd_err_t err;

    if (args->argc == 2)
    {
      uint32_t v = 0;

      err = swdCmRegRead(n, &v);
      cliPrintf("reg[%d]   : 0x%08X  %s\n", n, v, swdErrStr(err));
    }
    else
    {
      uint32_t v = (uint32_t)args->getData(2);

      err = swdCmRegWrite(n, v);
      cliPrintf("reg[%d]   <= 0x%08X  %s\n", n, v, swdErrStr(err));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "core") == true)
  {
    swd_cm_info_t info;
    swd_err_t     err = swdCmGetInfo(&info);

    if (err != SWD_OK)
    {
      cliPrintf("core     : %s\n", swdErrStr(err));
    }
    else
    {
      cliPrintf("CPUID    : 0x%08X  ARM %s r%dp%d\n",
                info.cpuid, info.core_name, info.rev_r, info.rev_p);
      if (info.id_addr)
      {
        cliPrintf("DEV ID   : 0x%08X @ 0x%08X\n", info.id_value, info.id_addr);
        cliPrintf("           dev_id 0x%03X  %s\n", info.dev_id, info.vendor);
      }
      else
      {
        cliPrintf("DEV ID   : 못 찾음 (알려진 위치에 없음)\n");
      }
    }
    ret = true;
  }

  // ---- 타깃 RAM 알고리즘 러너 -----------------------------------------

  // ELF 를 건드리기 전에 러너 자체를 검증한다. 손으로 짠 4바이트 기계어라
  // 파일도 파서도 필요 없고, 실패하면 원인이 바로 갈린다.
  if (args->argc >= 2 && args->isStr(0, "test") == true)
  {
    uint32_t base = 0x20001000;      // 타깃 RAM. 필요하면 인자로 바꾼다
    uint32_t a_val = 0, b_val = 0;
    swd_algo_test_t t;
    bool ok = false;

    if      (args->isStr(1, "alu")) { t = SWD_ALGO_TEST_ALU;     ok = true; }
    else if (args->isStr(1, "lr"))  { t = SWD_ALGO_TEST_LR;      ok = true; }
    else if (args->isStr(1, "to"))  { t = SWD_ALGO_TEST_TIMEOUT; ok = true; }

    if (ok)
    {
      uint32_t  ret_val = 0;
      uint32_t  ms = 0;
      swd_err_t err;

      if (args->argc >= 4)
      {
        a_val = (uint32_t)args->getData(2);
        b_val = (uint32_t)args->getData(3);
      }
      if (args->argc == 5)
      {
        base = (uint32_t)args->getData(4);
      }

      cliPrintf("test     : %s\n", swdAlgoTestName(t));
      cliPrintf("ram base : 0x%08X\n", base);

      err = swdAlgoSelfTest(base, t, a_val, b_val, &ret_val, &ms);

      if (t == SWD_ALGO_TEST_TIMEOUT)
      {
        // 여기서는 타임아웃이 나야 정상이다
        cliPrintf("result   : %s (%d ms)\n", swdErrStr(err), (int)ms);
        cliPrintf("           %s\n", (err == SWD_ERR_WAIT) ? "PASS (타임아웃 후 강제 정지)"
                                                            : "FAIL (타임아웃이 나야 한다)");
      }
      else
      {
        cliPrintf("args     : r0=%d r1=%d\n", (int)a_val, (int)b_val);
        cliPrintf("result   : %s (%d ms)\n", swdErrStr(err), (int)ms);
        if (err == SWD_OK)
        {
          cliPrintf("r0       : %d  ->  %s\n", (int)ret_val,
                    (ret_val == (a_val + b_val)) ? "PASS" : "FAIL (합이 안 맞는다)");
        }
        else
        {
          cliPrintf("           FAIL\n");
          if (t == SWD_ALGO_TEST_LR)
          {
            cliPrintf("  alu 는 되는데 lr 만 실패하면 LR 의 Thumb 비트를 의심한다\n");
          }
          else
          {
            cliPrintf("  swd regs 가 정상인데 여기서 실패하면 xPSR 의 T 비트를 의심한다\n");
          }
        }
      }
      ret = true;
    }
  }

  // 임의의 타깃 함수를 한 번 호출한다
  if (args->argc == 8 && args->isStr(0, "exec") == true)
  {
    swd_algo_ctx_t ctx;
    uint32_t  pc      = (uint32_t)args->getData(1);
    uint32_t  base    = (uint32_t)args->getData(2);
    uint32_t  ret_val = 0;
    swd_err_t err;

    err = swdAlgoSetup(&ctx, base, 0);
    if (err == SWD_OK)
    {
      err = swdAlgoCall(&ctx, pc,
                        (uint32_t)args->getData(3), (uint32_t)args->getData(4),
                        (uint32_t)args->getData(5), (uint32_t)args->getData(6),
                        (uint32_t)args->getData(7), &ret_val);
    }
    cliPrintf("exec     : pc 0x%08X  %s\n", pc, swdErrStr(err));
    cliPrintf("arena    : bkpt 0x%08X  stack 0x%08X\n", ctx.bkpt_addr, ctx.stack_top);
    cliPrintf("r0       : 0x%08X (%d)\n", ret_val, (int)ret_val);
    ret = true;
  }

  // ---- 내장 로직 애널라이저 -------------------------------------------
  //
  // 캡처는 원샷이고 8000샘플이면 20MSPS 에서 400us 만에 찬다. arm 만 해 두고
  // 사람이 다음 명령을 타이핑할 시간이 없으므로, 캡처 대상 동작을 같은
  // 명령 안에서 실행한다.
  //
  if (args->argc >= 2 && args->isStr(0, "la") == true)
  {
    if (args->argc == 2 && args->isStr(1, "connect") == true)
    {
      uint32_t  id = 0;
      swd_err_t err;

      swdLaArm(0);
      err = swdConnect(&id);
      swdLaFreeze();

      cliPrintf("connect  : %s  DPIDR 0x%08X\n", (err == SWD_OK) ? "OK" : "FAIL", id);
      cliPrintf("captured : %d sample @ %d KHz\n", (int)swdLaCount(), (int)swdLaRate());
      ret = true;
    }

    // 지정한 블록 읽기를 캡처한다. 실패 지점 앞뒤를 보기 위한 것.
    if (args->argc == 4 && args->isStr(1, "md") == true)
    {
      uint32_t addr = (uint32_t)args->getData(2);
      uint32_t cnt  = (uint32_t)args->getData(3);
      static uint32_t buf[64];
      swd_err_t err;

      if (cnt > 64) cnt = 64;

      swdLaArm(0);
      err = swdMemReadBlock(addr, buf, cnt);
      swdLaFreeze();

      cliPrintf("md %08X x%d : %s\n", addr, (int)cnt, swdErrStr(err));
      for (uint32_t i = 0; i < cnt; i += 4)
      {
        cliPrintf("%08X :", addr + i*4);
        for (uint32_t k = 0; k < 4 && (i+k) < cnt; k++) cliPrintf(" %08X", buf[i+k]);
        cliPrintf("\n");
      }
      cliPrintf("captured : %d sample @ %d KHz\n", (int)swdLaCount(), (int)swdLaRate());
      ret = true;
    }

    if (args->argc == 2 && args->isStr(1, "id") == true)
    {
      uint32_t id  = 0;
      uint32_t ack;

      swdLaArm(0);
      ack = swdTransfer(0, 1, SWD_DP_DPIDR, &id);
      swdLaFreeze();

      cliPrintf("ack      : %s  data 0x%08X\n", swdAckStr(ack), id);
      cliPrintf("captured : %d sample @ %d KHz\n", (int)swdLaCount(), (int)swdLaRate());
      ret = true;
    }

    // 도구 자체 검증용. 알려진 개수/주파수의 클럭을 내보내고 그대로 나오는지 본다.
    if (args->argc == 3 && args->isStr(1, "clk") == true)
    {
      uint32_t cnt = (uint32_t)args->getData(2);

      swdLaArm(0);
      SWDIO_OUT();
      SWDIO_LO();
      swdClock(cnt);
      swdLaFreeze();

      cliPrintf("clk %d 발생, %d sample @ %d KHz 캡처\n",
                (int)cnt, (int)swdLaCount(), (int)swdLaRate());
      ret = true;
    }

    if (args->argc == 2 && args->isStr(1, "stat") == true)
    {
      swd_la_stat_t st;

      if (swdLaAnalyze(&st) == true)
      {
        cliPrintf("samples  : %d @ %d KHz (%d ns/sample)\n",
                  (int)st.samples, (int)st.rate_khz, (int)(1000000UL/st.rate_khz));
        cliPrintf("clk moved: %s\n", st.clk_moved ? "yes" : "NO (핀이 안 움직임!)");
        cliPrintf("dio moved: %s\n", st.dio_moved ? "yes" : "NO (타깃 무응답 가능)");
        cliPrintf("edges    : %d\n", (int)st.edges);
        cliPrintf("freq     : avg %d KHz  min %d  max %d\n",
                  (int)st.f_avg_khz, (int)st.f_min_khz, (int)st.f_max_khz);
        cliPrintf("width    : hi %d ns  lo %d ns\n", (int)st.hi_ns, (int)st.lo_ns);
        cliPrintf("jitter   : %d ns\n", (int)st.jitter_ns);
      }
      else
      {
        cliPrintf("no capture\n");
      }
      ret = true;
    }

    if (args->argc >= 2 && args->isStr(1, "dump") == true)
    {
      swdLaDump((args->argc == 3) ? (uint32_t)args->getData(2) : 240);
      ret = true;
    }

    if (args->argc == 2 && args->isStr(1, "decode") == true)
    {
      swdLaDecode();
      ret = true;
    }

    if (args->argc == 2 && args->isStr(1, "rate") == true)
    {
      cliPrintf("rate     : %d KHz\n", (int)swdLaRate());
      ret = true;
    }

    if (args->argc == 3 && args->isStr(1, "rate") == true)
    {
      swdLaArm((uint32_t)args->getData(2));
      swdLaFreeze();
      cliPrintf("rate     : %d KHz\n", (int)swdLaRate());
      ret = true;
    }

    // 켜 두면 다음 실패한 트랜잭션에서 캡처가 얼어붙는다.
    // 간헐적 실패(Stage 2 의 1KB TAR 랩 같은)는 이 방법으로만 잡힌다.
    if (args->argc == 3 && args->isStr(1, "auto") == true)
    {
      bool on = args->isStr(2, "on");

      swdLaAutoSet(on);
      if (on) swdLaArm(0);
      cliPrintf("auto capture : %s\n", on ? "on (다음 실패 시 정지)" : "off");
      ret = true;
    }
  }

  if (ret != true)
  {
    cliPrintf("swd info\n");
    cliPrintf("swd pintest\n");
    cliPrintf("swd speed [khz]        0 = max\n");
    cliPrintf("swd connect [auto|loop]\n");
    cliPrintf("swd id\n");
    cliPrintf("swd reset\n");
    cliPrintf("swd scan\n");
    cliPrintf("swd clk <count>\n");
    cliPrintf("swd pin <clk|dio> <0|1>\n");
    cliPrintf("swd pinread\n");
    cliPrintf("swd drv [0~3]          출력 드라이브 0=LOW 3=VERY_HIGH\n");
    cliPrintf("\n");
    cliPrintf("swd power              디버그 파워업 + CTRL/STAT\n");
    cliPrintf("swd apid / apscan      AP IDR/CFG/BASE, AP 열거\n");
    cliPrintf("swd stat [clear]       링크 오류/복구 통계\n");
    cliPrintf("swd ap <n>             AP 선택\n");
    cliPrintf("swd dpread <a> / dpwrite <a> <v>\n");
    cliPrintf("swd md <addr> [cnt]    타깃 메모리 32bit 덤프\n");
    cliPrintf("swd mw <addr> <v> [cnt]\n");
    cliPrintf("swd mb|mh <addr>       8/16bit 읽기\n");
    cliPrintf("swd wb|wh <addr> <v>   8/16bit 쓰기\n");
    cliPrintf("swd bench <addr> <kb>  블록 읽기 처리량\n");
    cliPrintf("swd tartest <ram_addr> 1KB TAR 랩 검증 (RAM 주소!)\n");
    cliPrintf("\n");
    cliPrintf("swd halt|run|step|detach\n");
    cliPrintf("swd rsthalt            SYSRESETREQ + vector catch\n");
    cliPrintf("swd sysreset           리셋 후 자유 실행\n");
    cliPrintf("swd regs               R0~R15, xPSR, MSP, PSP, CONTROL\n");
    cliPrintf("swd reg <n> [v]        13=SP 14=LR 15=PC 16=xPSR\n");
    cliPrintf("swd core               CPUID 디코드 + 디바이스 ID\n");
    cliPrintf("\n");
    cliPrintf("swd test alu <a> <b> [ram]   러너 자가검증 (레지스터/실행/반환)\n");
    cliPrintf("swd test lr  <a> <b> [ram]   bx lr 복귀 경로\n");
    cliPrintf("swd test to                  타임아웃 경로\n");
    cliPrintf("swd exec <pc> <ram> <r0> <r1> <r2> <r3> <ms>\n");
    cliPrintf("\n");
    cliPrintf("swd la connect         캡처하며 connect\n");
    cliPrintf("swd la id              캡처하며 DPIDR 읽기\n");
    cliPrintf("swd la clk <n>         캡처하며 클럭 n개 (도구 자체 검증용)\n");
    cliPrintf("swd la stat            주파수/듀티/지터/에지수\n");
    cliPrintf("swd la dump [n]        ASCII 파형\n");
    cliPrintf("swd la decode          SWD 프로토콜 디코드\n");
    cliPrintf("swd la rate [khz]      샘플레이트\n");
    cliPrintf("swd la auto <on|off>   다음 실패 트랜잭션에서 자동 정지\n");
  }
}
#endif

#endif
