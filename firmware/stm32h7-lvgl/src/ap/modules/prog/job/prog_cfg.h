/*
 * prog_cfg.h
 *
 *  key = value 설정 파서.
 *
 *  디바이스 DB(/prog/mcu 의 txt 파일)와 펌웨어 매니페스트(fw.txt)가 같은 형식을 쓴다.
 *  JSON 파서를 넣지 않은 이유는 단순하다 — 적을 게 몇 줄 없고, 파서 하나를
 *  벤더링하면 .bss 를 수 KB 먹는데 여기서 필요한 건 50줄이다.
 *
 *  한 줄씩 흘려보낸다. 파일 전체를 메모리에 올리지 않으므로 DB 가 커져도
 *  버퍼는 한 줄 그대로다.
 */

#ifndef PROG_CFG_H_
#define PROG_CFG_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "ff.h"


#ifdef _USE_HW_SWD


#define CFG_SEC_MAX     48
#define CFG_KEY_MAX     16
#define CFG_VAL_MAX     80
#define CFG_LINE_MAX    160


/* 항목 하나마다 불린다. false 를 돌려주면 그 자리에서 파싱을 멈춘다 —
   찾던 걸 찾았을 때 나머지를 안 읽고 빠져나오라는 뜻이다.

   sec 는 마지막으로 나온 [섹션] 이름이고, 섹션 밖이면 빈 문자열이다. */
typedef bool (*cfg_cb_t)(const char *sec, const char *key, const char *val, void *ctx);

/* 파일을 훑는다. 파일이 없거나 열리지 않으면 false.
   콜백이 멈춰서 끝난 경우도 true 다 (그건 오류가 아니다). */
bool     cfgParse(const char *path, cfg_cb_t cb, void *ctx);

// "0x..." 와 10진수를 모두 받는다. 숫자가 아니면 0.
uint32_t cfgNum(const char *s);


#endif


#ifdef __cplusplus
}
#endif

#endif
