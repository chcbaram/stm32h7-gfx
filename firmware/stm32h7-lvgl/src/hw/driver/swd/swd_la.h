/*
 * swd_la.h
 *
 *  SWD 내장 로직 애널라이저.
 *
 *  TIM2 로 만든 샘플 클럭에 맞춰 DMA2 가 GPIO IDR 을 통째로 읽어 RAM_D2 에
 *  쌓는다. SWCLK/SWDIO 두 핀에 TIM 대체기능이 없어서 입력 캡처를 못 쓰고,
 *  비트뱅잉 중에는 MODER=출력이라 AF 입력 경로 자체가 끊기기 때문이다.
 *
 *  이걸 만드는 이유는 주파수 측정이 아니다. micros() 로도 충분하다.
 *  "내 코드가 틀렸는지 타깃이 응답을 안 하는지"를 구분하기 위해서다.
 *  SWD 실패는 전부 DPIDR=0xFFFFFFFF 하나로 수렴해서 콘솔로는 구분이 안 된다.
 *  SWDIO 를 같이 캡처하면 ACK 구간에서 타깃이 뭔가 내보내는지가 보이고,
 *  거기서 "프레이밍 버그" 와 "타깃 무응답" 이 갈린다.
 */

#ifndef SWD_LA_H_
#define SWD_LA_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_SWD


typedef struct
{
  uint32_t samples;         // 채워진 샘플 수
  uint32_t rate_khz;        // 실제 샘플레이트
  uint32_t edges;           // SWCLK 상승 에지 수
  uint32_t f_avg_khz;       // SWCLK 평균 주파수
  uint32_t f_min_khz;
  uint32_t f_max_khz;
  uint32_t hi_ns;           // 평균 high 폭
  uint32_t lo_ns;           // 평균 low 폭
  uint32_t jitter_ns;       // 최대 - 최소 주기
  bool     clk_moved;       // 핀이 실제로 토글됐는지
  bool     dio_moved;
} swd_la_stat_t;


bool     swdLaInit(void);
bool     swdLaIsInit(void);

bool     swdLaArm(uint32_t rate_khz);     // 원샷 캡처 무장
void     swdLaFreeze(void);               // 즉시 정지하고 버퍼 보존
bool     swdLaIsDone(void);
uint32_t swdLaCount(void);
uint32_t swdLaRate(void);

// idx 번째 샘플. 캡처가 끝난 뒤에만 의미가 있다.
bool     swdLaGet(uint32_t idx, uint8_t *p_clk, uint8_t *p_dio);
bool     swdLaAnalyze(swd_la_stat_t *p_stat);

// 실패한 트랜잭션을 잡기 위한 자동 캡처.
// 켜 두면 swd_transfer() 가 ACK != OK 로 빠질 때 캡처를 얼려서 보존한다.
// 100번에 1번 깨지는 현상은 외장 장비로는 트리거를 걸고 기다려야 하지만
// 이건 실패한 그 순간의 버퍼를 그대로 덤프할 수 있다.
void     swdLaAutoSet(bool enable);
bool     swdLaAutoGet(void);
void     swdLaAutoTrig(void);             // swd.c 가 실패 시 호출

#ifdef _USE_HW_CLI
void     swdLaDump(uint32_t count);       // ASCII 파형
void     swdLaDecode(void);               // SWD 프로토콜 디코드
#endif


#endif


#ifdef __cplusplus
}
#endif

#endif
