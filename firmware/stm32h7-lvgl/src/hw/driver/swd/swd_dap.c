/*
 * swd_dap.c
 *
 *  ARM ADIv5 Debug Access Port
 *
 *  주의할 지점 두 가지가 이 파일의 전부다.
 *
 *  1. AP 읽기는 posted 다.
 *     AP read 를 보내면 그 자리에서 값이 나오지 않고 "다음" AP read 나
 *     DP RDBUFF 읽기에서 나온다. 이걸 빠뜨리면 모든 워드가 한 칸씩 밀린
 *     채로 멀쩡해 보이는 값이 나온다.
 *
 *  2. TAR auto-increment 는 1KB 경계에서 랩한다.
 *     경계를 넘겨 블록 전송하면 1KB 마다 조용히 엉뚱한 주소에 쓰거나 읽는다.
 *     "플래시가 불안정한 것처럼" 보이기 때문에 원인을 찾기가 가장 어렵다.
 *     그래서 블록 함수는 무조건 경계에서 쪼갠다.
 */

#include "swd/swd_dap.h"


#ifdef _USE_HW_SWD


#define SWD_PWRUP_TIMEOUT_MS    100
#define SWD_CHUNK_RETRY         4


static bool     is_powered   = false;
static uint32_t select_cache = 0xFFFFFFFF;    // 무효값으로 시작
static uint32_t csw_cache    = 0xFFFFFFFF;
static uint8_t  ap_sel       = 0;

// 링크 오류와 복구 통계. 어디서 얼마나 깨지는지 알아야 대책이 선다.
static uint32_t stat_link_err;
static uint32_t stat_recover_ok;
static uint32_t stat_recover_ng;
static uint32_t stat_retry_ok;
static uint32_t stat_retry_ng;


static swd_err_t swdXfer(uint8_t ap_ndp, uint8_t rd_nwr, uint8_t addr, uint32_t *p_data);
static swd_err_t swdApSelect(uint8_t addr);
static swd_err_t swdCswSet(uint32_t csw);
static swd_err_t swdMemRead32Once(uint32_t addr, uint32_t *p_data);
static swd_err_t swdMemReadChunk(uint32_t addr, uint32_t *p_data, uint32_t count);
static swd_err_t swdMemWriteChunk(uint32_t addr, const uint32_t *p_data, uint32_t count);


// ----------------------------------------------------------------- 링크/파워업

swd_err_t swdDapEnsure(void)
{
  swd_err_t err;

  if (swdIsConnected() == false)
  {
    err = swdConnect(NULL);         // 내부에서 swdDapInvalidate() 가 불린다
    if (err != SWD_OK) return err;
  }

  if (is_powered == false)
  {
    err = swdDapPowerUp();
    if (err != SWD_OK) return err;
  }

  return SWD_OK;
}

swd_err_t swdDapPowerUp(void)
{
  swd_err_t err;
  uint32_t  ctrl = 0;
  uint32_t  t_start;

  // DPBANKSEL=0 으로 맞춰야 CTRL/STAT 이 보인다
  err = swdDpWrite(SWD_DP_SELECT, 0);
  if (err != SWD_OK) return err;
  select_cache = 0;

  swdDapClearError();

  err = swdDpWrite(SWD_DP_CTRL_STAT, SWD_PWRUP_REQ);
  if (err != SWD_OK) return err;

  t_start = millis();
  while (millis() - t_start < SWD_PWRUP_TIMEOUT_MS)
  {
    err = swdDpRead(SWD_DP_CTRL_STAT, &ctrl);
    if (err != SWD_OK) return err;

    if ((ctrl & SWD_PWRUP_ACK) == SWD_PWRUP_ACK)
    {
      is_powered = true;
      csw_cache  = 0xFFFFFFFF;
      return SWD_OK;
    }
  }

  return SWD_ERR_FAULT;
}

bool swdDapIsPowered(void)
{
  return is_powered;
}

void swdDapInvalidate(void)
{
  is_powered   = false;
  select_cache = 0xFFFFFFFF;
  csw_cache    = 0xFFFFFFFF;
}

void swdDapSetAp(uint8_t apsel)
{
  if (apsel != ap_sel)
  {
    ap_sel       = apsel;
    select_cache = 0xFFFFFFFF;      // AP 가 바뀌면 CSW 도 다시 써야 한다
    csw_cache    = 0xFFFFFFFF;
  }
}

uint8_t swdDapGetAp(void)
{
  return ap_sel;
}

/* sticky 에러를 지운다. swdXfer 를 다시 부르면 재귀가 되므로 raw 전송을 쓴다. */
swd_err_t swdDapClearError(void)
{
  uint32_t data;

  data = SWD_ABORT_ALLCLR;
  swdTransfer(0, 0, SWD_DP_ABORT, &data);

  return SWD_OK;
}


/* 순간적인 비트 오류로 링크가 죽었는지 판단한다. WAIT/FAULT 는 타깃이 응답은
   하고 있는 상태라 여기 포함하지 않는다. */
bool swdIsLinkErr(swd_err_t err)
{
  return (err == SWD_ERR_NORESP) || (err == SWD_ERR_PROTOCOL) || (err == SWD_ERR_PARITY);
}

swd_err_t swdDapRecover(void)
{
  uint32_t id = 0;

  swdLineReset();
  swdIdle(8);

  // line reset 뒤에는 DPIDR 을 한 번 읽어야 DP 가 reset state 를 벗어난다
  if (swdTransfer(0, 1, SWD_DP_DPIDR, &id) != SWD_ACK_OK)
  {
    return SWD_ERR_NORESP;
  }

  // 재동기 후 DP 상태를 모르므로 캐시를 버린다
  select_cache = 0xFFFFFFFF;
  csw_cache    = 0xFFFFFFFF;

  /* line reset 은 DP 를 리셋 상태로 되돌린다. CTRL/STAT 의 CDBGPWRUPREQ 까지
     지워지므로 파워업을 다시 해야 한다. 이걸 빠뜨리면 재동기는 되는데 이후
     AP 접근이 전부 실패해서, 복구가 아무 소용이 없다. */
  is_powered = false;

  return swdDapPowerUp();
}


// ----------------------------------------------------------------- DP/AP 레지스터

swd_err_t swdDapReadTargetId(uint32_t *p_id)
{
  swd_err_t err;
  uint32_t  dpidr = 0;
  uint32_t  val   = 0;

  if (p_id == NULL) return SWD_ERR_PROTOCOL;

  // DPv1 에는 TARGETID 가 없다
  err = swdDpRead(SWD_DP_DPIDR, &dpidr);
  if (err != SWD_OK)                  return err;
  if (((dpidr >> 12) & 0xF) < 2)      return SWD_ERR_PROTOCOL;

  /* SELECT 를 직접 건드리므로 캐시를 버려야 한다. 안 그러면 다음 AP 접근이
     DPBANKSEL 이 2 인 줄 모르고 그대로 쓴다. */
  err = swdDpWrite(SWD_DP_SELECT, 0x00000002);      // DPBANKSEL = 2
  if (err == SWD_OK) err = swdDpRead(0x4, &val);

  swdDpWrite(SWD_DP_SELECT, 0x00000000);
  swdDapInvalidate();

  if (err != SWD_OK) return err;
  if ((val & 1) == 0) return SWD_ERR_PROTOCOL;      // bit0 은 RAO 다

  *p_id = val;
  return SWD_OK;
}

swd_err_t swdDpRead(uint8_t addr, uint32_t *p_data)
{
  return swdXfer(0, 1, addr, p_data);
}

swd_err_t swdDpWrite(uint8_t addr, uint32_t data)
{
  return swdXfer(0, 0, addr, &data);
}

/* AP 읽기는 posted 다. 요청을 보낸 뒤 RDBUFF 에서 값을 거둬야 한다. */
swd_err_t swdApRead(uint8_t addr, uint32_t *p_data)
{
  swd_err_t err;
  uint32_t  dummy = 0;

  err = swdApSelect(addr);
  if (err != SWD_OK) return err;

  err = swdXfer(1, 1, addr & 0x0C, &dummy);
  if (err != SWD_OK) return err;

  return swdDpRead(SWD_DP_RDBUFF, p_data);
}

swd_err_t swdApWrite(uint8_t addr, uint32_t data)
{
  swd_err_t err;

  err = swdApSelect(addr);
  if (err != SWD_OK) return err;

  return swdXfer(1, 0, addr & 0x0C, &data);
}


// ----------------------------------------------------------------- 타깃 메모리

swd_err_t swdMemRead32(uint32_t addr, uint32_t *p_data)
{
  swd_err_t err;

  err = swdMemRead32Once(addr, p_data);
  if (swdIsLinkErr(err) && swdDapRecover() == SWD_OK)
  {
    err = swdMemRead32Once(addr, p_data);
  }
  return err;
}

static swd_err_t swdMemRead32Once(uint32_t addr, uint32_t *p_data)
{
  swd_err_t err;

  err = swdDapEnsure();                                       if (err != SWD_OK) return err;
  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_OFF | SWD_CSW_SIZE_32);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;

  return swdApRead(SWD_AP_DRW, p_data);
}

swd_err_t swdMemWrite32(uint32_t addr, uint32_t data)
{
  swd_err_t err;

  err = swdDapEnsure();                                       if (err != SWD_OK) return err;
  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_OFF | SWD_CSW_SIZE_32);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;

  return swdApWrite(SWD_AP_DRW, data);
}

/* 8/16비트 접근은 DRW 안에서 주소에 해당하는 레인에 실린다.
   32비트 워드 안의 어느 바이트/하프워드인지는 TAR 하위 비트가 정한다. */
swd_err_t swdMemRead16(uint32_t addr, uint16_t *p_data)
{
  swd_err_t err;
  uint32_t  value = 0;

  err = swdDapEnsure();                                       if (err != SWD_OK) return err;
  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_OFF | SWD_CSW_SIZE_16);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;
  err = swdApRead(SWD_AP_DRW, &value);                        if (err != SWD_OK) return err;

  if (p_data != NULL)
  {
    *p_data = (uint16_t)(value >> ((addr & 0x02) * 8));
  }
  return SWD_OK;
}

swd_err_t swdMemWrite16(uint32_t addr, uint16_t data)
{
  swd_err_t err;

  err = swdDapEnsure();                                       if (err != SWD_OK) return err;
  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_OFF | SWD_CSW_SIZE_16);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;

  return swdApWrite(SWD_AP_DRW, ((uint32_t)data) << ((addr & 0x02) * 8));
}

swd_err_t swdMemRead8(uint32_t addr, uint8_t *p_data)
{
  swd_err_t err;
  uint32_t  value = 0;

  err = swdDapEnsure();                                       if (err != SWD_OK) return err;
  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_OFF | SWD_CSW_SIZE_8);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;
  err = swdApRead(SWD_AP_DRW, &value);                        if (err != SWD_OK) return err;

  if (p_data != NULL)
  {
    *p_data = (uint8_t)(value >> ((addr & 0x03) * 8));
  }
  return SWD_OK;
}

swd_err_t swdMemWrite8(uint32_t addr, uint8_t data)
{
  swd_err_t err;

  err = swdDapEnsure();                                       if (err != SWD_OK) return err;
  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_OFF | SWD_CSW_SIZE_8);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;

  return swdApWrite(SWD_AP_DRW, ((uint32_t)data) << ((addr & 0x03) * 8));
}

swd_err_t swdMemReadBlock(uint32_t addr, uint32_t *p_data, uint32_t count)
{
  swd_err_t err;

  if ((addr & 3) || p_data == NULL) return SWD_ERR_PROTOCOL;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  while (count > 0)
  {
    // TAR auto-increment 가 1KB 경계에서 랩하므로 반드시 쪼갠다
    uint32_t room  = (SWD_TAR_WRAP - (addr & (SWD_TAR_WRAP - 1))) / 4;
    uint32_t chunk = (count < room) ? count : room;

    /* 링크만 다시 세우고 이 청크를 통째로 다시 한다. 전송 단위로 재시도하면
       TAR 이 이미 증가해 있어서 엉뚱한 주소를 읽는다. */
    for (int try = 0; try < SWD_CHUNK_RETRY; try++)
    {
      err = swdMemReadChunk(addr, p_data, chunk);
      if (err == SWD_OK)
      {
        if (try > 0) stat_retry_ok++;
        break;
      }
      if (swdIsLinkErr(err) == false) break;

      stat_link_err++;
      if (swdDapRecover() == SWD_OK) stat_recover_ok++;
      else                         { stat_recover_ng++; break; }
    }
    if (err != SWD_OK) { stat_retry_ng++; return err; }

    addr   += chunk * 4;
    p_data += chunk;
    count  -= chunk;
  }

  return SWD_OK;
}

swd_err_t swdMemWriteBlock(uint32_t addr, const uint32_t *p_data, uint32_t count)
{
  swd_err_t err;

  if ((addr & 3) || p_data == NULL) return SWD_ERR_PROTOCOL;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  while (count > 0)
  {
    uint32_t room  = (SWD_TAR_WRAP - (addr & (SWD_TAR_WRAP - 1))) / 4;
    uint32_t chunk = (count < room) ? count : room;

    for (int try = 0; try < SWD_CHUNK_RETRY; try++)
    {
      err = swdMemWriteChunk(addr, p_data, chunk);
      if (err == SWD_OK)
      {
        if (try > 0) stat_retry_ok++;
        break;
      }
      if (swdIsLinkErr(err) == false) break;

      stat_link_err++;
      if (swdDapRecover() == SWD_OK) stat_recover_ok++;
      else                         { stat_recover_ng++; break; }
    }
    if (err != SWD_OK) { stat_retry_ng++; return err; }

    addr   += chunk * 4;
    p_data += chunk;
    count  -= chunk;
  }

  return SWD_OK;
}

swd_err_t swdMemFill(uint32_t addr, uint32_t data, uint32_t count)
{
  swd_err_t err;

  if (addr & 3) return SWD_ERR_PROTOCOL;

  err = swdDapEnsure();
  if (err != SWD_OK) return err;

  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_SINGLE | SWD_CSW_SIZE_32);
  if (err != SWD_OK) return err;

  while (count > 0)
  {
    uint32_t room  = (SWD_TAR_WRAP - (addr & (SWD_TAR_WRAP - 1))) / 4;
    uint32_t chunk = (count < room) ? count : room;

    err = swdApWrite(SWD_AP_TAR, addr);                       if (err != SWD_OK) return err;
    err = swdApSelect(SWD_AP_DRW);                            if (err != SWD_OK) return err;

    for (uint32_t i = 0; i < chunk; i++)
    {
      uint32_t value = data;

      err = swdXfer(1, 0, SWD_AP_DRW & 0x0C, &value);         if (err != SWD_OK) return err;
    }

    addr  += chunk * 4;
    count -= chunk;
  }

  return SWD_OK;
}

void swdDapGetStat(uint32_t *p_err, uint32_t *p_rec_ok, uint32_t *p_rec_ng,
                   uint32_t *p_retry_ok, uint32_t *p_retry_ng)
{
  if (p_err)      *p_err      = stat_link_err;
  if (p_rec_ok)   *p_rec_ok   = stat_recover_ok;
  if (p_rec_ng)   *p_rec_ng   = stat_recover_ng;
  if (p_retry_ok) *p_retry_ok = stat_retry_ok;
  if (p_retry_ng) *p_retry_ng = stat_retry_ng;
}

void swdDapClearStat(void)
{
  stat_link_err = stat_recover_ok = stat_recover_ng = 0;
  stat_retry_ok = stat_retry_ng = 0;
}

const char *swdErrStr(swd_err_t err)
{
  switch(err)
  {
    case SWD_OK:            return "OK";
    case SWD_ERR_PIN:       return "PIN (핀이 토글되지 않음)";
    case SWD_ERR_NORESP:    return "NO-RESP (타깃 무응답)";
    case SWD_ERR_WAIT:      return "WAIT (재시도 소진)";
    case SWD_ERR_FAULT:     return "FAULT";
    case SWD_ERR_PARITY:    return "PARITY";
    case SWD_ERR_PROTOCOL:  return "PROTOCOL";
    case SWD_ERR_BUSY:      return "BUSY";
    default:                return "?";
  }
}


// ----------------------------------------------------------------- 내부

/* WAIT 는 패킷 전체를 다시 보낸다. 타깃이 이전 트랜잭션을 아직 처리 중이라는
   뜻이라 조금 기다렸다 재시도하면 대개 풀린다. */
swd_err_t swdXfer(uint8_t ap_ndp, uint8_t rd_nwr, uint8_t addr, uint32_t *p_data)
{
  uint32_t ack = 0;

  for (uint32_t retry = 0; retry < HW_SWD_WAIT_RETRY; retry++)
  {
    ack = swdTransfer(ap_ndp, rd_nwr, addr, p_data);

    if (ack == SWD_ACK_OK)
    {
      return SWD_OK;
    }
    if (ack == SWD_ACK_WAIT)
    {
      if (retry >= 8)
      {
        delayUs(retry);           // 오래 끌면 점점 여유를 준다
      }
      continue;
    }
    break;
  }

  switch(ack)
  {
    case SWD_ACK_WAIT:
      {
        uint32_t data = SWD_ABORT_DAPABORT;
        swdTransfer(0, 0, SWD_DP_ABORT, &data);
      }
      return SWD_ERR_WAIT;

    case SWD_ACK_FAULT:
      swdDapClearError();
      return SWD_ERR_FAULT;

    case SWD_ACK_PARITY:   return SWD_ERR_PARITY;
    case SWD_ACK_NORESP:   return SWD_ERR_NORESP;
    default:               return SWD_ERR_PROTOCOL;
  }
}

/* AP 주소는 8비트다. 상위 니블은 SELECT.APBANKSEL 로, 하위 A[3:2] 만
   패킷에 실린다. DPBANKSEL 은 항상 0 으로 둔다 (CTRL/STAT 접근 조건). */
swd_err_t swdApSelect(uint8_t addr)
{
  uint32_t sel = ((uint32_t)ap_sel << 24) | (addr & 0xF0);

  if (sel == select_cache)
  {
    return SWD_OK;
  }

  swd_err_t err = swdDpWrite(SWD_DP_SELECT, sel);
  if (err == SWD_OK)
  {
    select_cache = sel;
  }
  else
  {
    select_cache = 0xFFFFFFFF;
  }
  return err;
}

swd_err_t swdCswSet(uint32_t csw)
{
  if (csw == csw_cache)
  {
    return SWD_OK;
  }

  swd_err_t err = swdApWrite(SWD_AP_CSW, csw);
  csw_cache = (err == SWD_OK) ? csw : 0xFFFFFFFF;

  return err;
}

/* 1KB 경계를 넘지 않는 한 덩어리를 읽는다.
   AP read 가 posted 라서 n 워드에 n+1 트랜잭션이 든다.
     TAR -> AP read(버림) -> AP read x (n-1) -> RDBUFF */
static swd_err_t swdMemReadChunk(uint32_t addr, uint32_t *p_data, uint32_t count)
{
  swd_err_t err;
  uint32_t  dummy = 0;

  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_SINGLE | SWD_CSW_SIZE_32);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;
  err = swdApSelect(SWD_AP_DRW);                              if (err != SWD_OK) return err;

  // 파이프라인 채우기. 첫 결과는 버린다.
  err = swdXfer(1, 1, SWD_AP_DRW & 0x0C, &dummy);             if (err != SWD_OK) return err;

  for (uint32_t i = 0; i < count - 1; i++)
  {
    err = swdXfer(1, 1, SWD_AP_DRW & 0x0C, &p_data[i]);       if (err != SWD_OK) return err;
  }

  return swdDpRead(SWD_DP_RDBUFF, &p_data[count - 1]);
}

static swd_err_t swdMemWriteChunk(uint32_t addr, const uint32_t *p_data, uint32_t count)
{
  swd_err_t err;
  uint32_t  ctrl = 0;

  err = swdCswSet(SWD_CSW_BASE | SWD_CSW_INC_SINGLE | SWD_CSW_SIZE_32);
                                                              if (err != SWD_OK) return err;
  err = swdApWrite(SWD_AP_TAR, addr);                         if (err != SWD_OK) return err;
  err = swdApSelect(SWD_AP_DRW);                              if (err != SWD_OK) return err;

  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t value = p_data[i];

    err = swdXfer(1, 0, SWD_AP_DRW & 0x0C, &value);           if (err != SWD_OK) return err;
  }

  // 쓰기는 워드마다 확인하지 않고 블록 끝에서 한 번만 본다. 이게 처리량의 핵심.
  err = swdDpRead(SWD_DP_CTRL_STAT, &ctrl);                   if (err != SWD_OK) return err;

  if (ctrl & (SWD_CTRL_STICKYERR | SWD_CTRL_WDATAERR))
  {
    swdDapClearError();
    return SWD_ERR_FAULT;
  }
  return SWD_OK;
}


#endif
