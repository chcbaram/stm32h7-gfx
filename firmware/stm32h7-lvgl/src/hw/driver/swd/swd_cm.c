/*
 * swd_cm.c
 *
 *  Cortex-M 디버그 코어
 *
 *  주의할 지점
 *
 *  1. C_MASKINTS 는 halt 상태에서만 바꿀 수 있고, C_HALT 를 클리어하는 같은
 *     쓰기에서 바꾸면 동작이 정의되지 않는다. 그래서 "인터럽트 막고 실행"은
 *     반드시 두 번에 나눠 쓴다.
 *
 *  2. PC(REGSEL 15)는 Thumb 비트를 지우고 쓰고, 대신 xPSR 의 T 비트를 세운다.
 *     T 가 없으면 실행 즉시 INVSTATE UsageFault 로 떨어진다.
 *
 *  3. 리셋은 링크가 죽는 걸 전제로 짠다. SYSRESETREQ 가 DP 까지 리셋하는
 *     패밀리가 있어서, 폴링 중 에러가 나면 재연결하고 계속 기다려야 한다.
 */

#include "swd/swd_cm.h"
#include "swd/swd_dap.h"


#ifdef _USE_HW_SWD


#define CM_REG_TIMEOUT_MS     100
#define CM_HALT_TIMEOUT_MS    500
#define CM_RESET_TIMEOUT_MS   1000


/* 디바이스 ID 레지스터 위치는 벤더/패밀리마다 다르다.
   여기 있는 건 Stage 7 의 devices.txt id_addr 를 코드로 미리 흉내 낸 것이고,
   나중에는 이 표가 아니라 SD 카드의 DB 가 진짜 소스가 된다. */
typedef struct
{
  uint32_t    addr;
  uint32_t    mask;
  const char *desc;
} cm_idloc_t;

static const cm_idloc_t id_loc_tbl[] =
{
  { 0xE0042000, 0x00000FFF, "ST DBGMCU (F1/F2/F4/F7/L0/L1/L4/G4)" },
  { 0x5C001000, 0x00000FFF, "ST DBGMCU (H7)"                      },
  { 0x40015800, 0x00000FFF, "ST DBGMCU (G0/C0)"                   },
  { 0xE0044000, 0x00000FFF, "ST DBGMCU (L5/U5/H5)"                },
  { 0x10000100, 0xFFFFFFFF, "Nordic FICR.INFO.PART"               },
};


static uint32_t  last_dfsr;

static swd_err_t swdCmWaitRegRdy(void);


// ----------------------------------------------------------------- 코어 제어

swd_err_t swdCmHalt(void)
{
  swd_err_t err;
  uint32_t  dhcsr = 0;
  uint32_t  t_start;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  err = swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_HALT);
  if (err != SWD_OK) return err;

  t_start = millis();
  while (millis() - t_start < CM_HALT_TIMEOUT_MS)
  {
    err = swdMemRead32(CM_DHCSR, &dhcsr);
    if (err != SWD_OK) return err;

    if (dhcsr & CM_S_HALT)
    {
      return SWD_OK;
    }
  }
  return SWD_ERR_WAIT;
}

swd_err_t swdCmRun(void)
{
  swd_err_t err;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  // C_MASKINTS 가 켜져 있었다면 halt 상태에서 먼저 내린다 (같은 쓰기에서 금지)
  err = swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_HALT);
  if (err != SWD_OK) return err;

  return swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN);
}

swd_err_t swdCmStep(void)
{
  swd_err_t err;
  uint32_t  dhcsr = 0;
  uint32_t  t_start;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  err = swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_STEP);
  if (err != SWD_OK) return err;

  t_start = millis();
  while (millis() - t_start < CM_HALT_TIMEOUT_MS)
  {
    err = swdMemRead32(CM_DHCSR, &dhcsr);
    if (err != SWD_OK) return err;
    if (dhcsr & CM_S_HALT) return SWD_OK;
  }
  return SWD_ERR_WAIT;
}

swd_err_t swdCmIsHalted(bool *p_halted)
{
  swd_err_t err;
  uint32_t  dhcsr = 0;

  err = swdMemRead32(CM_DHCSR, &dhcsr);
  if (p_halted != NULL)
  {
    *p_halted = (err == SWD_OK) && ((dhcsr & CM_S_HALT) != 0);
  }
  return err;
}

swd_err_t swdCmGetDhcsr(uint32_t *p_dhcsr)
{
  return swdMemRead32(CM_DHCSR, p_dhcsr);
}

swd_err_t swdCmDetach(void)
{
  swd_err_t err;

  err = swdMemWrite32(CM_DEMCR, CM_DEMCR_TRCENA);
  if (err != SWD_OK) return err;

  // C_DEBUGEN 을 내리면 코어가 디버그에서 완전히 풀린다
  return swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY);
}


// ----------------------------------------------------------------- 리셋

swd_err_t swdCmResetHalt(void)
{
  swd_err_t err;
  uint32_t  dhcsr = 0;
  uint32_t  t_start;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  // 이미 죽어 있을 수도 있으므로 halt 실패는 무시한다
  swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_HALT);

  err = swdMemWrite32(CM_DEMCR, CM_DEMCR_TRCENA | CM_DEMCR_VC_CORERESET);
  if (err != SWD_OK) return err;

  swdMemWrite32(CM_DFSR, CM_DFSR_ALLCLR);

  // 이 쓰기의 응답은 못 받을 수 있다. 타깃이 그 자리에서 리셋되기 때문.
  swdMemWrite32(CM_AIRCR, CM_AIRCR_KEY | CM_SYSRESETREQ);

  delay(2);

  /* 폴링 중 링크가 죽으면 재연결한다. SYSRESETREQ 가 디버그 로직까지
     리셋하는 패밀리가 있다. */
  t_start = millis();
  while (millis() - t_start < CM_RESET_TIMEOUT_MS)
  {
    if (swdMemRead32(CM_DHCSR, &dhcsr) == SWD_OK)
    {
      if (dhcsr & CM_S_HALT)
      {
        // 지우기 전에 사유를 남긴다. 지운 뒤에 읽으면 항상 none 이 나온다.
        last_dfsr = 0;
        swdMemRead32(CM_DFSR, &last_dfsr);

        swdMemWrite32(CM_DEMCR, CM_DEMCR_TRCENA);     // VC_CORERESET 해제
        swdMemWrite32(CM_DFSR, CM_DFSR_ALLCLR);
        return SWD_OK;
      }
      continue;
    }

    // 링크가 끊겼다. 다시 붙이고 계속 기다린다.
    delay(1);
    if (swdConnect(NULL) == SWD_OK)
    {
      swdDapPowerUp();
      swdMemWrite32(CM_DHCSR, CM_DHCSR_KEY | CM_C_DEBUGEN | CM_C_HALT);
      swdMemWrite32(CM_DEMCR, CM_DEMCR_TRCENA | CM_DEMCR_VC_CORERESET);
    }
  }

  return SWD_ERR_WAIT;
}

swd_err_t swdCmSysReset(void)
{
  swd_err_t err;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  swdMemWrite32(CM_DEMCR, CM_DEMCR_TRCENA);           // vector catch 해제
  swdMemWrite32(CM_AIRCR, CM_AIRCR_KEY | CM_SYSRESETREQ);

  delay(10);

  // 리셋으로 링크가 끊겼을 수 있다
  if (swdMemRead32(CM_DHCSR, NULL) != SWD_OK)
  {
    swdConnect(NULL);
    swdDapPowerUp();
  }
  return SWD_OK;
}

uint32_t swdCmGetLastDfsr(void)
{
  return last_dfsr;
}


// ----------------------------------------------------------------- 레지스터

swd_err_t swdCmRegRead(uint8_t regsel, uint32_t *p_data)
{
  swd_err_t err;

  err = swdMemWrite32(CM_DCRSR, regsel);      if (err != SWD_OK) return err;
  err = swdCmWaitRegRdy();                    if (err != SWD_OK) return err;

  return swdMemRead32(CM_DCRDR, p_data);
}

swd_err_t swdCmRegWrite(uint8_t regsel, uint32_t data)
{
  swd_err_t err;

  err = swdMemWrite32(CM_DCRDR, data);                        if (err != SWD_OK) return err;
  err = swdMemWrite32(CM_DCRSR, regsel | CM_REG_WRITE);       if (err != SWD_OK) return err;

  return swdCmWaitRegRdy();
}


// ----------------------------------------------------------------- 식별

swd_err_t swdCmGetInfo(swd_cm_info_t *p_info)
{
  swd_err_t err;
  uint32_t  partno;

  if (p_info == NULL) return SWD_ERR_PROTOCOL;

  memset(p_info, 0, sizeof(swd_cm_info_t));

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  err = swdMemRead32(CM_CPUID, &p_info->cpuid);
  if (err != SWD_OK) return err;

  partno         = (p_info->cpuid >> 4) & 0xFFF;
  p_info->rev_r  = (uint8_t)((p_info->cpuid >> 20) & 0xF);
  p_info->rev_p  = (uint8_t)(p_info->cpuid & 0xF);

  switch(partno)
  {
    case 0xC20: p_info->core_name = "Cortex-M0";  break;
    case 0xC60: p_info->core_name = "Cortex-M0+"; break;
    case 0xC21: p_info->core_name = "Cortex-M1";  break;
    case 0xC23: p_info->core_name = "Cortex-M3";  break;
    case 0xC24: p_info->core_name = "Cortex-M4";  break;
    case 0xC27: p_info->core_name = "Cortex-M7";  break;
    case 0xD20: p_info->core_name = "Cortex-M23"; break;
    case 0xD21: p_info->core_name = "Cortex-M33"; break;
    default:    p_info->core_name = "unknown";    break;
  }

  /* 알려진 ID 레지스터 위치를 순회한다. 없는 주소는 FAULT 가 나므로 그냥
     건너뛰면 된다. 벤더가 늘어도 표에 줄만 추가하면 되고, Stage 7 부터는
     이 표 대신 SD 카드의 devices.txt 가 소스가 된다. */
  for (uint32_t i = 0; i < sizeof(id_loc_tbl)/sizeof(id_loc_tbl[0]); i++)
  {
    uint32_t value = 0;

    if (swdMemRead32(id_loc_tbl[i].addr, &value) != SWD_OK)
    {
      swdDapClearError();
      continue;
    }
    if (value == 0 || value == 0xFFFFFFFF)
    {
      continue;
    }

    p_info->id_addr  = id_loc_tbl[i].addr;
    p_info->id_value = value;
    p_info->dev_id   = (uint16_t)(value & id_loc_tbl[i].mask);
    p_info->vendor   = id_loc_tbl[i].desc;
    break;
  }

  return SWD_OK;
}

const char *swdCmDfsrStr(uint32_t dfsr)
{
  if (dfsr & CM_DFSR_EXTERNAL) return "EXTERNAL";
  if (dfsr & CM_DFSR_VCATCH)   return "VCATCH (reset vector catch)";
  if (dfsr & CM_DFSR_DWTTRAP)  return "DWTTRAP";
  if (dfsr & CM_DFSR_BKPT)     return "BKPT";
  if (dfsr & CM_DFSR_HALTED)   return "HALTED (debugger request)";
  return "none";
}


// ----------------------------------------------------------------- 내부

swd_err_t swdCmWaitRegRdy(void)
{
  uint32_t dhcsr = 0;
  uint32_t t_start = millis();

  while (millis() - t_start < CM_REG_TIMEOUT_MS)
  {
    swd_err_t err = swdMemRead32(CM_DHCSR, &dhcsr);

    if (err != SWD_OK) return err;
    if (dhcsr & CM_S_REGRDY) return SWD_OK;
  }
  return SWD_ERR_WAIT;
}


#endif
