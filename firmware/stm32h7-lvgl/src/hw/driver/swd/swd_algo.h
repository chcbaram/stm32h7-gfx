/*
 * swd_algo.h
 *
 *  타깃 RAM 알고리즘 러너.
 *
 *  플래시를 굽는 코드는 결국 타깃 자신의 RAM 에서 돌아야 한다. 여기가 그
 *  실행 엔진이고, Stage 5 의 .FLM / .stldr 이 이 위에 올라간다.
 *
 *  하는 일은 단순하다. 코드를 타깃 RAM 에 올려두고, 레지스터에 인자를 넣고,
 *  PC 를 함수 진입점으로 돌린 뒤 실행시켜 BKPT 에서 멈추면 R0 를 거둔다.
 *  C 함수 호출 규약(AAPCS)을 SWD 로 흉내 내는 셈이다.
 */

#ifndef SWD_ALGO_H_
#define SWD_ALGO_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"
#include "swd.h"


#ifdef _USE_HW_SWD


/* 아레나 배치 (ram_base 기준 오프셋)
 *
 *   +0x000   0xBE00BE00       bkpt 트램폴린. LR 이 여기를 가리킨다.
 *   +0x010   알고리즘 코드
 *   ...      알고리즘 데이터 / .bss
 *   stack_top 까지가 스택 (아래로 자란다)
 *
 * 트램폴린에 BKPT #0 을 두 개 넣는 건 정렬 때문이다. 어느 하프워드로
 * 진입해도 BKPT 를 만난다.
 */
#define SWD_ALGO_BKPT_OFFS    0x000
#define SWD_ALGO_CODE_OFFS    0x010
#define SWD_ALGO_BKPT_WORD    0xBE00BE00UL


typedef struct
{
  uint32_t code_addr;     // 코드가 로드된 주소
  uint32_t bkpt_addr;     // BKPT 트램폴린. 함수가 bx lr 로 돌아올 자리
  uint32_t stack_top;     // 8바이트 정렬 (AAPCS)
  uint32_t static_base;   // R9 로 넣을 값. RWPI 알고리즘용. 0 이면 건너뜀
} swd_algo_ctx_t;

typedef enum
{
  SWD_ALGO_TEST_ALU = 0,  // adds r0,r0,r1 / bkpt  - 레지스터·실행·정지·반환
  SWD_ALGO_TEST_LR,       // adds r0,r0,r1 / bx lr - 실제 알고리즘과 같은 복귀 경로
  SWD_ALGO_TEST_TIMEOUT,  // b .                   - 타임아웃과 강제 정지
} swd_algo_test_t;


// ram_base 를 받아 아레나를 잡고 BKPT 트램폴린을 심는다.
swd_err_t swdAlgoSetup(swd_algo_ctx_t *p_ctx, uint32_t ram_base, uint32_t stack_size);

// 타깃에서 함수를 한 번 호출한다. 반환값(R0)은 p_ret 로.
swd_err_t swdAlgoCall(const swd_algo_ctx_t *p_ctx, uint32_t pc,
                      uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                      uint32_t timeout_ms, uint32_t *p_ret);

/* ELF 를 건드리기 전에 러너 자체를 검증한다.
   손으로 짠 4바이트 기계어라 파일도 파서도 필요 없다.
   ALU 는 되는데 LR 이 안 되면 LR 의 Thumb 비트 문제,
   둘 다 안 되는데 swd regs 가 정상이면 xPSR 의 T 비트 문제다. */
swd_err_t swdAlgoSelfTest(uint32_t ram_base, swd_algo_test_t test,
                          uint32_t a, uint32_t b,
                          uint32_t *p_ret, uint32_t *p_ms);

const char *swdAlgoTestName(swd_algo_test_t test);


#endif


#ifdef __cplusplus
}
#endif

#endif
