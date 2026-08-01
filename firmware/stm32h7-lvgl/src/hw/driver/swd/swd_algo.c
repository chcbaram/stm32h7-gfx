/*
 * swd_algo.c
 *
 *  타깃 RAM 알고리즘 러너
 *
 *  놓치기 쉬운 세 가지
 *
 *  1. LR 은 Thumb 비트를 세우고, PC 는 지운다.
 *     LR 은 분기 대상이라 bit0 이 Thumb 상태를 뜻한다. 지우면 bx lr 로
 *     돌아올 때 ARM 모드로 해석돼 트램폴린에 도달하지 못하고, 전부
 *     타임아웃으로 끝난다. 반대로 PC(REGSEL 15)는 bit0 을 지워서 쓰고
 *     Thumb 상태는 xPSR 의 T 비트로 알린다.
 *
 *  2. xPSR 의 T 비트를 반드시 세운다.
 *     안 그러면 첫 명령에서 INVSTATE UsageFault 로 떨어진다.
 *
 *  3. C_MASKINTS 는 halt 상태에서만, C_HALT 를 내리는 쓰기와 별도로 바꾼다.
 *     알고리즘이 도는 동안 타깃 인터럽트가 끼어들면 안 되므로 마스킹하는데,
 *     같은 쓰기에서 실행까지 시키면 동작이 정의되지 않는다.
 */

#include "swd/swd_algo.h"
#include "swd/swd_cm.h"
#include "swd/swd_dap.h"


#ifdef _USE_HW_SWD


#define ALGO_POLL_MS        1
#define ALGO_STACK_DEF      0x800


/* 자가 검증용 블롭. 전부 4바이트 Thumb 기계어 두 개씩이다.
     0xBE001840 = adds r0,r0,r1 (0x1840) / bkpt #0 (0xBE00)
     0x47701840 = adds r0,r0,r1 (0x1840) / bx lr   (0x4770)
     0xE7FEE7FE = b .           (0xE7FE) x2                  */
static const uint32_t algo_blob[] =
{
  [SWD_ALGO_TEST_ALU]     = 0xBE001840,
  [SWD_ALGO_TEST_LR]      = 0x47701840,
  [SWD_ALGO_TEST_TIMEOUT] = 0xE7FEE7FE,
};


// ----------------------------------------------------------------- 실행

swd_err_t swdAlgoSetup(swd_algo_ctx_t *p_ctx, uint32_t ram_base, uint32_t stack_size)
{
  swd_err_t err;

  if (p_ctx == NULL) return SWD_ERR_PROTOCOL;

  if (stack_size == 0)
  {
    stack_size = ALGO_STACK_DEF;
  }

  ram_base = (ram_base + 7) & ~7UL;         // AAPCS 8바이트 정렬

  p_ctx->bkpt_addr   = ram_base + SWD_ALGO_BKPT_OFFS;
  p_ctx->code_addr   = ram_base + SWD_ALGO_CODE_OFFS;
  p_ctx->stack_top   = (p_ctx->code_addr + stack_size + 7) & ~7UL;
  p_ctx->static_base = 0;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  // 함수가 bx lr 로 돌아올 자리에 BKPT 를 심어 둔다
  return swdMemWrite32(p_ctx->bkpt_addr, SWD_ALGO_BKPT_WORD);
}

swd_err_t swdAlgoCall(const swd_algo_ctx_t *p_ctx, uint32_t pc,
                      uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                      uint32_t timeout_ms, uint32_t *p_ret)
{
  swd_err_t err;
  uint32_t  dhcsr = 0;
  uint32_t  dfsr  = 0;
  uint32_t  t_start;

  if (p_ctx == NULL) return SWD_ERR_PROTOCOL;

  err = swdCmHalt();
  if (err != SWD_OK) return err;

  // halt 상태에서 인터럽트를 막는다. 실행 지시는 아직 하지 않는다.
  err = swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_HALT | CM_C_MASKINTS);
  if (err != SWD_OK) return err;

  err = swdCmRegWrite(0, r0);                             if (err != SWD_OK) return err;
  err = swdCmRegWrite(1, r1);                             if (err != SWD_OK) return err;
  err = swdCmRegWrite(2, r2);                             if (err != SWD_OK) return err;
  err = swdCmRegWrite(3, r3);                             if (err != SWD_OK) return err;

  if (p_ctx->static_base != 0)
  {
    err = swdCmRegWrite(9, p_ctx->static_base);           if (err != SWD_OK) return err;
  }

  err = swdCmRegWrite(CM_REG_SP, p_ctx->stack_top);       if (err != SWD_OK) return err;
  err = swdCmRegWrite(CM_REG_LR, p_ctx->bkpt_addr | 1);   if (err != SWD_OK) return err;
  err = swdCmRegWrite(CM_REG_PC, pc & ~1UL);              if (err != SWD_OK) return err;
  err = swdCmRegWrite(CM_REG_XPSR, CM_XPSR_THUMB);        if (err != SWD_OK) return err;

  // 이전 정지 사유를 지워야 이번 결과를 믿을 수 있다
  err = swdMemWrite32(CM_DFSR, CM_DFSR_ALLCLR);           if (err != SWD_OK) return err;

  // C_HALT 만 내린다. C_MASKINTS 는 그대로 둔다.
  err = swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_MASKINTS);
  if (err != SWD_OK) return err;

  t_start = millis();
  while (1)
  {
    err = swdMemRead32(CM_DHCSR, &dhcsr);
    if (err != SWD_OK) return err;

    if (dhcsr & CM_S_LOCKUP)
    {
      swdCmHalt();
      return SWD_ERR_FAULT;                 // 코어가 락업. 잘못된 코드거나 폴트
    }
    if (dhcsr & CM_S_HALT)
    {
      break;
    }
    if (millis() - t_start >= timeout_ms)
    {
      swdCmHalt();                          // 무한루프여도 세워 놓고 나간다
      return SWD_ERR_WAIT;
    }
    delay(ALGO_POLL_MS);
  }

  swdMemRead32(CM_DFSR, &dfsr);

  if (p_ret != NULL)
  {
    err = swdCmRegRead(0, p_ret);
    if (err != SWD_OK) return err;
  }

  // 다음 호출 준비를 위해 C_MASKINTS 를 내려 둔다 (halt 상태에서)
  swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_HALT);

  // BKPT 로 멈춘 게 아니면 알고리즘이 의도대로 끝나지 않은 것이다
  if ((dfsr & CM_DFSR_BKPT) == 0)
  {
    return SWD_ERR_PROTOCOL;
  }

  return SWD_OK;
}


// ----------------------------------------------------------------- 자가 검증

swd_err_t swdAlgoSelfTest(uint32_t ram_base, swd_algo_test_t test,
                          uint32_t a, uint32_t b,
                          uint32_t *p_ret, uint32_t *p_ms)
{
  swd_algo_ctx_t ctx;
  swd_err_t      err;
  uint32_t       t_start;
  uint32_t       timeout;

  if (test > SWD_ALGO_TEST_TIMEOUT) return SWD_ERR_PROTOCOL;

  err = swdAlgoSetup(&ctx, ram_base, 0);
  if (err != SWD_OK) return err;

  err = swdMemWrite32(ctx.code_addr, algo_blob[test]);
  if (err != SWD_OK) return err;

  /* Cortex-M4 에는 I-cache 가 없어서 방금 쓴 코드를 바로 실행해도 된다.
     M7 타깃이면 여기서 캐시 무효화가 필요하다. */

  timeout = (test == SWD_ALGO_TEST_TIMEOUT) ? 200 : 1000;

  t_start = millis();
  err = swdAlgoCall(&ctx, ctx.code_addr, a, b, 0, 0, timeout, p_ret);
  if (p_ms != NULL)
  {
    *p_ms = millis() - t_start;
  }

  return err;
}

const char *swdAlgoTestName(swd_algo_test_t test)
{
  switch(test)
  {
    case SWD_ALGO_TEST_ALU:     return "alu (adds r0,r0,r1 / bkpt)";
    case SWD_ALGO_TEST_LR:      return "lr  (adds r0,r0,r1 / bx lr)";
    case SWD_ALGO_TEST_TIMEOUT: return "to  (b . 무한루프)";
    default:                    return "?";
  }
}


#endif
