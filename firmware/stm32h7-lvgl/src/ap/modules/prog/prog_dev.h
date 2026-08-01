/*
 * prog_dev.h
 *
 *  디바이스 DB.
 *
 *  /prog/mcu 의 txt 파일을 전부 읽는다. 벤더별로 파일을 나눠도 되고 한 파일에
 *  몰아도 된다. 항목을 메모리에 쌓아두지 않고 필요할 때마다 훑는다 — 104개면
 *  13KB 쯤 되는데, SD 읽기가 100ms 도 안 걸리는 일에 그만한 .bss 를 상시로
 *  잡아둘 이유가 없다.
 *
 *  자동 판별의 핵심은 id_addr / id_mask / id_val 3연이다.
 *
 *    "어느 주소를 읽어 어느 비트가 무엇이면 이 디바이스"
 *
 *  이렇게만 적으면 ST 의 DBGMCU 든 Nordic 의 FICR 든 Microchip 의 DSU 든
 *  코드 변경 없이 DB 항목만 추가하면 된다. ID 레지스터 위치가 벤더마다 다른
 *  닭-달걀 문제는, DB 에 등장하는 서로 다른 id_addr 만 순회해서 푼다.
 */

#ifndef PROG_DEV_H_
#define PROG_DEV_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "swd.h"


#ifdef _USE_HW_SWD


#define DEV_NAME_MAX    40
#define DEV_CPU_MAX     16
#define DEV_PATH_MAX    64
#define DEV_ADDR_MAX    16      // 순회할 서로 다른 id_addr 개수


typedef struct
{
  char     name[DEV_NAME_MAX];
  char     cpu[DEV_CPU_MAX];
  uint32_t id_addr;
  uint32_t id_mask;
  uint32_t id_val;
  uint32_t ram;
  uint32_t ram_sz;
  /* 알고리즘이 들고 있는 값과 교차 검증하는 용도다. 알고리즘 파일이 자기
     크기를 틀리게 적은 경우가 실제로 있다 — GigaDevice 팩의 1MB/2MB .FLM 이
     둘 다 3840KB 라고 한다. 그러면 범위 검사가 무력해진다. 0 이면 모른다. */
  uint32_t flash;
  uint32_t flash_sz;
  char     algo[DEV_PATH_MAX];    // 기본 알고리즘 경로 (비어 있을 수 있다)
  bool     is_valid;
} prog_dev_t;


// 이름으로 찾는다. 대소문자를 구분하지 않고, 앞부분만 맞아도 된다.
bool     devFind(const char *name, prog_dev_t *p_dev);

/* 타깃에서 읽어 판별한다. swdConnect 와 파워업이 끝난 뒤에 불러야 한다.
     SWD_OK              찾았다
     SWD_ERR_NORESP      아무 항목도 맞지 않았다
     SWD_ERR_FAULT       둘 이상이 맞았다 (이름을 직접 지정해야 한다)
   p_id 에는 실제로 읽은 값을 돌려준다. 못 찾았을 때 진단에 쓴다. */
swd_err_t devDetect(prog_dev_t *p_dev, uint32_t *p_id);

// DB 를 훑는다. cb 가 false 를 돌려주면 멈춘다. 돌려주는 값은 훑은 개수.
uint32_t devList(bool (*cb)(const prog_dev_t *p_dev, void *ctx), void *ctx);


#endif


#ifdef __cplusplus
}
#endif

#endif
