/*
 * swd_dap.h
 *
 *  ARM ADIv5 Debug Access Port 계층.
 *
 *  swd.c 의 패킷 전송 위에 DP/AP 레지스터 접근과 MEM-AP 를 통한 타깃 메모리
 *  읽기/쓰기를 얹는다. 이 계층까지 벤더 무관한 ARM 표준이다.
 */

#ifndef SWD_DAP_H_
#define SWD_DAP_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"
#include "swd.h"


#ifdef _USE_HW_SWD


// DP CTRL/STAT
#define SWD_CTRL_CSYSPWRUPACK   (1UL << 31)
#define SWD_CTRL_CSYSPWRUPREQ   (1UL << 30)
#define SWD_CTRL_CDBGPWRUPACK   (1UL << 29)
#define SWD_CTRL_CDBGPWRUPREQ   (1UL << 28)
#define SWD_CTRL_CDBGRSTACK     (1UL << 27)
#define SWD_CTRL_CDBGRSTREQ     (1UL << 26)
#define SWD_CTRL_WDATAERR       (1UL <<  7)
#define SWD_CTRL_READOK         (1UL <<  6)
#define SWD_CTRL_STICKYERR      (1UL <<  5)
#define SWD_CTRL_STICKYCMP      (1UL <<  4)
#define SWD_CTRL_STICKYORUN     (1UL <<  1)
#define SWD_CTRL_ORUNDETECT     (1UL <<  0)

#define SWD_PWRUP_REQ           (SWD_CTRL_CSYSPWRUPREQ | SWD_CTRL_CDBGPWRUPREQ)
#define SWD_PWRUP_ACK           (SWD_CTRL_CSYSPWRUPACK | SWD_CTRL_CDBGPWRUPACK)

// DP ABORT
#define SWD_ABORT_DAPABORT      (1UL <<  0)
#define SWD_ABORT_STKCMPCLR     (1UL <<  1)
#define SWD_ABORT_STKERRCLR     (1UL <<  2)
#define SWD_ABORT_WDERRCLR      (1UL <<  3)
#define SWD_ABORT_ORUNERRCLR    (1UL <<  4)
#define SWD_ABORT_ALLCLR        0x0000001EUL

// MEM-AP 레지스터 (APBANKSEL<<4 | A[3:2])
#define SWD_AP_CSW              0x00
#define SWD_AP_TAR              0x04
#define SWD_AP_DRW              0x0C
#define SWD_AP_BD0              0x10
#define SWD_AP_CFG              0xF4
#define SWD_AP_BASE             0xF8
#define SWD_AP_IDR              0xFC

// MEM-AP CSW
#define SWD_CSW_BASE            0x23000000UL    // MstrDbg | HPROT | Reserved
#define SWD_CSW_SIZE_8          0x0
#define SWD_CSW_SIZE_16         0x1
#define SWD_CSW_SIZE_32         0x2
#define SWD_CSW_INC_OFF         (0UL << 4)
#define SWD_CSW_INC_SINGLE      (1UL << 4)

// TAR auto-increment 경계. ADIv5 는 구현 정의지만 최소 1KB 는 보장된다.
// 이 경계를 넘겨서 블록 전송하면 1KB 마다 조용히 데이터가 깨진다.
#define SWD_TAR_WRAP            0x400


// 링크가 새로 맺어지면 캐시(SELECT/CSW)와 파워업 상태를 버려야 한다.
// swd.c 의 swdConnect() 가 호출한다.
void      swdDapInvalidate(void);

// 연결 + 디버그 파워업까지 보장한다. 이미 되어 있으면 아무것도 안 한다.
swd_err_t swdDapEnsure(void);
swd_err_t swdDapPowerUp(void);
bool      swdDapIsPowered(void);

swd_err_t swdDpRead(uint8_t addr, uint32_t *p_data);
swd_err_t swdDpWrite(uint8_t addr, uint32_t data);
swd_err_t swdApRead(uint8_t addr, uint32_t *p_data);
swd_err_t swdApWrite(uint8_t addr, uint32_t data);

void      swdDapSetAp(uint8_t apsel);
uint8_t   swdDapGetAp(void);
swd_err_t swdDapClearError(void);

/* 링크를 다시 세운다. line reset -> DPIDR 재동기 -> sticky 클리어.
   순간적인 비트 오류로 DP 가 응답을 멈췄을 때 쓴다.
   SELECT/CSW 캐시가 무효화되므로 호출한 쪽은 CSW/TAR 부터 다시 세워야 한다. */
swd_err_t swdDapRecover(void);
bool      swdIsLinkErr(swd_err_t err);

// 링크 오류/복구 통계
void      swdDapGetStat(uint32_t *p_err, uint32_t *p_rec_ok, uint32_t *p_rec_ng,
                        uint32_t *p_retry_ok, uint32_t *p_retry_ng);
void      swdDapClearStat(void);

// 타깃 메모리
swd_err_t swdMemRead32(uint32_t addr, uint32_t *p_data);
swd_err_t swdMemWrite32(uint32_t addr, uint32_t data);
swd_err_t swdMemRead16(uint32_t addr, uint16_t *p_data);
swd_err_t swdMemWrite16(uint32_t addr, uint16_t data);
swd_err_t swdMemRead8(uint32_t addr, uint8_t *p_data);
swd_err_t swdMemWrite8(uint32_t addr, uint8_t data);

swd_err_t swdMemReadBlock(uint32_t addr, uint32_t *p_data, uint32_t count);
swd_err_t swdMemWriteBlock(uint32_t addr, const uint32_t *p_data, uint32_t count);
swd_err_t swdMemFill(uint32_t addr, uint32_t data, uint32_t count);

const char *swdErrStr(swd_err_t err);


#endif


#ifdef __cplusplus
}
#endif

#endif
