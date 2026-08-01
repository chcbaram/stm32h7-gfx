/*
 * swd_cm.h
 *
 *  Cortex-M 디버그 코어 제어.
 *
 *  전부 MEM-AP 를 통한 32비트 접근이고, 레지스터 주소는 ARMv7-M/ARMv8-M
 *  공통이라 벤더 무관하다. 코어를 세우고, 레지스터를 읽고 쓰고, 리셋한다.
 *  Stage 4 의 알고리즘 러너가 이 위에 올라간다.
 */

#ifndef SWD_CM_H_
#define SWD_CM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"
#include "swd.h"


#ifdef _USE_HW_SWD


// 디버그 레지스터 (코어 자신의 메모리 맵)
#define CM_CPUID          0xE000ED00
#define SWD_CM_AP_MAX     8       // 훑어볼 AP 개수
#define CM_AIRCR          0xE000ED0C
#define CM_DFSR           0xE000ED30
#define CM_DHCSR          0xE000EDF0
#define CM_DCRSR          0xE000EDF4
#define CM_DCRDR          0xE000EDF8
#define CM_DEMCR          0xE000EDFC
#define CM_FP_CTRL        0xE0002000

// DHCSR 쓰기는 상위 16비트에 키가 필요하다
#define CM_DHCSR_KEY      0xA05F0000
#define CM_C_DEBUGEN      (1UL << 0)
#define CM_C_HALT         (1UL << 1)
#define CM_C_STEP         (1UL << 2)
#define CM_C_MASKINTS     (1UL << 3)
// DHCSR 읽기 상태 비트
#define CM_S_REGRDY       (1UL << 16)
#define CM_S_HALT         (1UL << 17)
#define CM_S_SLEEP        (1UL << 18)
#define CM_S_LOCKUP       (1UL << 19)
#define CM_S_RETIRE_ST    (1UL << 24)
#define CM_S_RESET_ST     (1UL << 25)

#define CM_AIRCR_KEY      0x05FA0000
#define CM_VECTRESET      (1UL << 0)
#define CM_VECTCLRACTIVE  (1UL << 1)
#define CM_SYSRESETREQ    (1UL << 2)

#define CM_DEMCR_VC_CORERESET (1UL << 0)
#define CM_DEMCR_VC_HARDERR   (1UL << 10)
#define CM_DEMCR_MON_EN       (1UL << 16)
#define CM_DEMCR_TRCENA       (1UL << 24)

// DFSR (write-1-to-clear)
#define CM_DFSR_HALTED    (1UL << 0)
#define CM_DFSR_BKPT      (1UL << 1)
#define CM_DFSR_DWTTRAP   (1UL << 2)
#define CM_DFSR_VCATCH    (1UL << 3)
#define CM_DFSR_EXTERNAL  (1UL << 4)
#define CM_DFSR_ALLCLR    0x1F

// DCRSR REGSEL
#define CM_REG_R0         0
#define CM_REG_SP         13
#define CM_REG_LR         14
#define CM_REG_PC         15
#define CM_REG_XPSR       16
#define CM_REG_MSP        17
#define CM_REG_PSP        18
#define CM_REG_CTRL       20      // CONTROL/FAULTMASK/BASEPRI/PRIMASK
#define CM_REG_WRITE      (1UL << 16)

// 타깃 코드를 실행하기 전에 항상 이 값을 xPSR 에 써야 한다.
// T 비트가 없으면 즉시 INVSTATE UsageFault 가 난다.
#define CM_XPSR_THUMB     0x01000000


typedef struct
{
  uint32_t    cpuid;
  const char *core_name;    // "Cortex-M4"
  uint8_t     rev_r;
  uint8_t     rev_p;
  uint32_t    id_addr;      // 디바이스 ID 를 찾은 주소 (0 이면 못 찾음)
  uint32_t    id_value;
  uint16_t    dev_id;
  const char *vendor;
} swd_cm_info_t;


swd_err_t   swdCmHalt(void);
swd_err_t   swdCmRun(void);
swd_err_t   swdCmStep(void);
swd_err_t   swdCmIsHalted(bool *p_halted);
swd_err_t   swdCmGetDhcsr(uint32_t *p_dhcsr);

// nRST 없이 리셋한다. SYSRESETREQ + DEMCR.VC_CORERESET 조합.
// 일부 패밀리는 리셋이 DP 까지 날려서 링크가 끊기므로, 그 경우 재연결하며
// 계속 폴링한다. 처음부터 그렇게 짜지 않으면 나중에 반드시 물린다.
swd_err_t   swdCmResetHalt(void);

/* 코어 디버그가 닿는 AP 를 고른다. 파트에 따라 AP0 이 아니다 —
   STM32H7S3 은 AP1(APB) 뒤에 있다. 연결 직후 한 번 부르면 된다. */
swd_err_t   swdCmEnsureAp(void);
void        swdCmSetAp(uint8_t apsel);
void        swdCmInvalidate(void);
// resetHalt/halt 가 DFSR 을 지우기 직전에 읽어 둔 값. 정지 사유 확인용.
uint32_t    swdCmGetLastDfsr(void);
swd_err_t   swdCmSysReset(void);

// 디버그를 놓고 타깃을 자유 실행시킨다 (C_DEBUGEN=0)
swd_err_t   swdCmDetach(void);

swd_err_t   swdCmRegRead(uint8_t regsel, uint32_t *p_data);
swd_err_t   swdCmRegWrite(uint8_t regsel, uint32_t data);

swd_err_t   swdCmGetInfo(swd_cm_info_t *p_info);
const char *swdCmDfsrStr(uint32_t dfsr);


#endif


#ifdef __cplusplus
}
#endif

#endif
