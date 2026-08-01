/*
 * prog.h
 *
 *  오프라인 다운로더 모듈.
 *
 *  SD카드의 펌웨어와 플래시 알고리즘을 읽어 타깃에 굽는다. 아래 계층
 *  (swd.c / swd_dap.c / swd_cm.c / swd_algo.c)은 전부 hw 쪽에 있고,
 *  여기는 파일과 잡 흐름을 다룬다.
 */

#ifndef PROG_H_
#define PROG_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"


#ifdef _USE_HW_SWD


bool progInit(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
