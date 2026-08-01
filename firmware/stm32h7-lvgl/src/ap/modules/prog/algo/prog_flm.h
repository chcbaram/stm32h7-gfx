/*
 * prog_flm.h
 *
 *  CMSIS-Pack 플래시 알고리즘(.FLM) 구현.
 *
 *  ARM 표준이라 벤더 중립이다. Nordic / NXP / Renesas / Infineon / Microchip /
 *  GigaDevice 가 전부 CMSIS-Pack 으로 배포하므로, ST 이외의 MCU 를 지원하는
 *  기본 경로가 여기다. 벤더가 늘어도 이 파일은 바뀌지 않는다 — 늘어나는 건
 *  .FLM 파일과 디바이스 DB 항목뿐이다.
 *
 *  성공 반환값이 0 이다 (.stldr 은 1). 극성은 flmCall 하나에 가둬둔다.
 */

#ifndef PROG_FLM_H_
#define PROG_FLM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "prog/algo/prog_algo.h"


#ifdef _USE_HW_SWD


// FlashDevice.DevType
#define FLM_DEV_ONCHIP      1
#define FLM_DEV_EXTSPI      5


extern const algo_ops_t flm_ops;


#endif


#ifdef __cplusplus
}
#endif

#endif
