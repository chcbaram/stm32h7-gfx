/*
 * prog_hex.h
 *
 *  Intel HEX 스트리밍 리더.
 *
 *  .bin 과 달리 주소가 파일 안에 있고, .elf 와 달리 그 주소가 레코드마다 흩어져
 *  있다. 800KB 짜리 hex 를 메모리에 펼치지 않고 굽기 위해, 페이지가 낮은 주소부터
 *  차례로 요청된다는 점을 이용해 파일을 한 번만 앞으로 훑는다.
 *
 *  그래서 레코드가 주소 오름차순이어야 한다. 열 때 전체를 한 번 스캔하면서
 *  이걸 확인하므로, 어긋난 파일은 플래시를 지우기 전에 걸린다.
 */

#ifndef PROG_HEX_H_
#define PROG_HEX_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "ff.h"


#ifdef _USE_HW_SWD


#define HEX_LINE_MAX      552     // ":" + LL(2) + AAAA(4) + TT(2) + 255*2 + CC(2) + CRLF
#define HEX_DATA_MAX      255
#define HEX_BUF_SIZE      512     // 32 배수 (sd.c 의 캐시 무효화 때문)

// 레코드 타입
#define HEX_REC_DATA      0x00
#define HEX_REC_EOF       0x01
#define HEX_REC_EXT_SEG   0x02
#define HEX_REC_START_SEG 0x03
#define HEX_REC_EXT_LIN   0x04
#define HEX_REC_START_LIN 0x05


typedef struct
{
  uint8_t  buf[HEX_BUF_SIZE] __attribute__((aligned(32)));
  uint32_t buf_len;
  uint32_t buf_pos;

  FIL      file;
  bool     is_open;

  // 열 때의 전체 스캔 결과
  uint32_t lo;              // 가장 낮은 데이터 주소
  uint32_t hi;              // 가장 높은 데이터 주소 + 1
  uint32_t data_bytes;      // 데이터 레코드의 LL 합
  uint32_t rec_cnt;
  uint32_t entry;           // 05/03 레코드. 없으면 has_entry = false
  bool     has_entry;

  // 스트리밍 상태
  uint32_t base;            // 02/04 레코드가 만든 상위 주소
  uint32_t rec_addr;
  uint32_t rec_len;
  uint8_t  rec_data[HEX_DATA_MAX];
  bool     rec_valid;       // 아직 소비되지 않은 레코드가 물려 있다
  bool     at_eof;
} hex_t;


/* 열면서 파일 전체를 한 번 스캔한다. 체크섬 오류, 주소 역행, 빈 파일을 여기서
   전부 걸러내므로 이후 굽기 중에 실패할 일이 없다. */
bool hexOpen(hex_t *p_hex, const char *path);

// 확장자가 아니라 첫 글자가 ':' 인지로 판단한다.
bool hexIsHexFile(const char *path);
void hexClose(hex_t *p_hex);

// 스트리밍 위치를 파일 처음으로 되돌린다. 굽기와 검증이 각각 한 번씩 부른다.
bool hexRewind(hex_t *p_hex);

/* [addr, addr+len) 을 채운다. 데이터가 없는 구간은 empty 로 남긴다.
   addr 은 호출할 때마다 커져야 한다 (스트리밍 전제). */
bool hexFill(hex_t *p_hex, uint32_t addr, uint8_t *p_buf, uint32_t len, uint8_t empty);


#endif


#ifdef __cplusplus
}
#endif

#endif
