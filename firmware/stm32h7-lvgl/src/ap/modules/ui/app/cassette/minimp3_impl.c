/* minimp3 구현 TU.
 *
 * 헤더 전용 라이브러리(minimp3)의 실제 구현을 이 파일에서만 컴파일한다.
 * 다른 곳에서는 minimp3.h 를 include 해도 선언만 들어온다.
 *
 * MINIMP3_ONLY_MP3 : MP1/MP2 테이블 제거 (코드 크기 축소, MP3 만 재생)
 */
#include "ap_def.h"

#ifdef _USE_HW_LVGL

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#endif
