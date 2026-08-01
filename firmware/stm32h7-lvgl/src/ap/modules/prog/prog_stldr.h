/*
 * prog_stldr.h
 *
 *  ST 로더(.stldr) 구현. CubeProgrammer 가 쓰는 것과 같은 파일이다.
 *
 *  두 종류가 있고 인터페이스는 같다.
 *
 *    FlashLoader/<DEV_ID>.stldr      MCU 내부 플래시. 파일명이 DEV_ID 라
 *                                    타깃에서 읽은 값으로 바로 고를 수 있다.
 *    ExternalLoader/<칩>_<보드>.stldr 외부 QSPI / NOR / SDRAM
 *
 *  ST 전용 포맷이므로 이 파일은 벤더 종속이다. 다른 제조사는 .FLM 으로 간다
 *  (prog_flm.c). 그래서 여기가 없어도 다운로더는 동작해야 한다.
 *
 *  .FLM 과 다른 점
 *    - 절대 주소로 링크되어 있다. 재배치하면 안 된다.
 *    - 성공 반환값이 1 이다 (.FLM 은 0).
 *    - SectorErase 가 범위를, Write 가 임의 길이를 받는다.
 *    - UnInit 이 없다.
 *    - 소거·굽기 병렬도를 인자로 받는다. .FLM 보다 빠른 이유가 이것이다.
 */

#ifndef PROG_STLDR_H_
#define PROG_STLDR_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "prog/prog_algo.h"


#ifdef _USE_HW_SWD


// StorageInfo.DeviceType (ST Dev_Inf.h)
#define STLDR_DEV_MCU_FLASH     1
#define STLDR_DEV_NAND_FLASH    2
#define STLDR_DEV_NOR_FLASH     3
#define STLDR_DEV_SRAM          4
#define STLDR_DEV_PSRAM         5
#define STLDR_DEV_PC_CARD       6
#define STLDR_DEV_SPI_FLASH     7
#define STLDR_DEV_I2C_FLASH     8
#define STLDR_DEV_SDRAM         9
#define STLDR_DEV_I2C_EEPROM    10


extern const algo_ops_t stldr_ops;


#endif


#ifdef __cplusplus
}
#endif

#endif
