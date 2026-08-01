/*
 * swd.h
 *
 *  SWD (Serial Wire Debug) 전송 계층.
 *
 *  GPIO 비트뱅잉으로 SWCLK/SWDIO 파형을 직접 만든다. 타이밍이 중요해서
 *  gpio_tbl[] 을 거치지 않고 이 드라이버가 핀을 직접 소유한다.
 *
 *  이 파일은 물리 계층까지만 담당한다. DP/AP 접근(ADIv5)과 Cortex-M 디버그는
 *  swd/swd_dap.h, swd/swd_cm.h 로 올라간다.
 */

#ifndef SWD_H_
#define SWD_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_SWD


// 타깃이 응답하는 3비트 ACK. LSB first 로 받으므로 OK 는 0b001 = 1 이다.
//
#define SWD_ACK_OK        0x1
#define SWD_ACK_WAIT      0x2
#define SWD_ACK_FAULT     0x4
#define SWD_ACK_NORESP    0x7     // 아무도 드라이브하지 않음(풀업). 타깃 무응답
#define SWD_ACK_PARITY    0x8     // 3비트 ACK 가 아닌 자체 코드. 데이터 패리티 불일치

// DP 레지스터 주소 A[3:2]
//
#define SWD_DP_DPIDR      0x0     // read
#define SWD_DP_ABORT      0x0     // write
#define SWD_DP_CTRL_STAT  0x4
#define SWD_DP_SELECT     0x8     // write
#define SWD_DP_RESEND     0x8     // read
#define SWD_DP_RDBUFF     0xC     // read


typedef enum
{
  SWD_OK = 0,
  SWD_ERR_PIN,        // 핀이 토글되지 않음. MODER/배선/단락
  SWD_ERR_NORESP,     // 타깃 무응답
  SWD_ERR_WAIT,       // WAIT 재시도 소진
  SWD_ERR_FAULT,      // FAULT
  SWD_ERR_PARITY,
  SWD_ERR_PROTOCOL,
  SWD_ERR_BUSY,
  SWD_ERR_ABORT,      // 사람이 중단시켰다. 오류가 아니다.
  SWD_ERR_MISMATCH,   // fw.txt 가 말한 MCU 와 실제로 물린 MCU 가 다르다
} swd_err_t;


bool      swdInit(void);
bool      swdIsInit(void);
bool      swdIsBusy(void);

// 속도. 요청값과 실측값을 분리해서 노출한다. 비트뱅잉이라 요청한 1MHz 가
// 실제로 1MHz 라는 보장이 없고, 그 괴리 자체가 유용한 진단이다.
//
bool      swdSetSpeed(uint32_t khz);        // khz 0 이면 최대 속도(딜레이 없음)
uint32_t  swdGetSpeed(void);                // 마지막으로 요청된 kHz
uint32_t  swdGetSpeedActual(void);          // 최근 실측 kHz
uint32_t  swdMeasureSpeed(void);            // 지금 실측해서 갱신 후 반환

// 핀이 실제로 움직이는지 확인한다. 출력 모드에서도 IDR 은 실제 패드 상태를
// 반영하므로, 이걸로 MODER 설정 오류나 GND 단락을 잡을 수 있다.
// 속도 실측은 CPU 루프 시간만 재기 때문에 핀이 죽어 있어도 멀쩡한 값이 나온다.
//
bool      swdPinTest(void);

// 링크
//
swd_err_t swdConnect(uint32_t *p_idcode);
swd_err_t swdConnectAuto(uint32_t *p_idcode, uint32_t *p_khz);
void      swdDisconnect(void);
bool      swdIsConnected(void);

// 저수준. 상위 계층(swd_dap.c)이 쓴다.
//
void      swdLineReset(void);
void      swdJtagToSwd(void);
void      swdClock(uint32_t count);                 // SWDIO 를 놔둔 채 클럭만
void      swdIdle(uint32_t count);                  // SWDIO=0 으로 클럭
uint32_t  swdTransfer(uint8_t ap_ndp, uint8_t rd_nwr, uint8_t addr, uint32_t *p_data);
const char *swdAckStr(uint32_t ack);

// 핀 정보. 배선은 swd.c 가 단일 소스로 갖고 있고, 로직 애널라이저(swd_la.c)가
// DMA 소스 주소와 비트 위치를 알아야 해서 접근자로만 노출한다.
//
volatile uint32_t *swdGetClkIdr(void);
volatile uint32_t *swdGetIoIdr(void);
uint8_t   swdGetClkPin(void);
uint8_t   swdGetIoPin(void);

// nRST. 미배선이면 swdHasRst() 가 false 를 반환하고 set 은 무시된다.
// 나중에 핀을 물릴 때 호출부를 고치지 않아도 되게 지금 API 를 확정해 둔다.
//
bool      swdHasRst(void);
void      swdRstSet(bool level);

// 확장 예약. ADIv5.2 이상 파트는 dormant 상태에서 깨워야 한다.
// STM32F4 같은 DPv1 파트는 필요 없어서 지금은 빈 함수다.
//
void      swdSelectDormantExit(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
