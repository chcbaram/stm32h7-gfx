/*
 * prog_elf.h
 *
 *  ELF32 스트리밍 리더.
 *
 *  .FLM(CMSIS-Pack)과 .stldr(ST External Loader)이 둘 다 ELF32 라서 파서를
 *  공유한다. 파일을 통째로 메모리에 올리지 않고 f_lseek/f_read 로 필요한
 *  부분만 읽는다. .FLM 은 13KB 정도지만 .stldr 은 수백 KB 짜리도 있다.
 */

#ifndef PROG_ELF_H_
#define PROG_ELF_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "ap_def.h"
#include "ff.h"


#ifdef _USE_HW_SWD


// 섹션 타입
#define ELF_SHT_PROGBITS    1
#define ELF_SHT_SYMTAB      2
#define ELF_SHT_STRTAB      3
#define ELF_SHT_NOBITS      8

// 섹션 플래그
#define ELF_SHF_WRITE       0x1
#define ELF_SHF_ALLOC       0x2
#define ELF_SHF_EXECINSTR   0x4

// 세그먼트 타입
#define ELF_PT_LOAD         1

// 심볼 타입 (st_info & 0xF)
#define ELF_STT_OBJECT      1
#define ELF_STT_FUNC        2

#define ELF_SEC_NAME_MAX    24


typedef struct
{
  FIL      file;
  bool     is_open;

  uint16_t e_type;          // 1=REL 2=EXEC 3=DYN
  uint16_t e_machine;       // 40=ARM
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;

  uint32_t shstr_off;       // 섹션 이름 문자열 테이블의 파일 오프셋
  uint32_t symtab_idx;      // SHT_SYMTAB 섹션 인덱스. 없으면 0xFFFFFFFF
} elf_t;

typedef struct
{
  char     name[ELF_SEC_NAME_MAX];
  uint32_t type;
  uint32_t flags;
  uint32_t addr;
  uint32_t offset;
  uint32_t size;
  uint32_t link;
  uint32_t entsize;
} elf_sec_t;

typedef struct
{
  uint32_t type;
  uint32_t offset;
  uint32_t vaddr;
  uint32_t filesz;
  uint32_t memsz;
  uint32_t flags;
} elf_phdr_t;

/* 로드 콜백. 섹션/세그먼트를 조각내어 전달하므로 타깃에 그대로 흘려보내면 된다.
   NOBITS(.bss)는 p_data 가 NULL 이고 그만큼 0 으로 채우라는 뜻이다. */
typedef bool (*elf_load_cb_t)(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx);


bool     elfOpen(elf_t *p_elf, const char *path);
void     elfClose(elf_t *p_elf);
bool     elfIsArm32(elf_t *p_elf);

bool     elfRead(elf_t *p_elf, uint32_t offset, void *p_buf, uint32_t len);
uint32_t elfSecCount(elf_t *p_elf);
bool     elfGetSec(elf_t *p_elf, uint32_t idx, elf_sec_t *p_sec);
bool     elfFindSec(elf_t *p_elf, const char *name, elf_sec_t *p_sec);
bool     elfGetPhdr(elf_t *p_elf, uint32_t idx, elf_phdr_t *p_phdr);

// Thumb 함수 심볼은 값의 bit0 이 서 있다. 그대로 돌려주니 호출부가 판단한다.
bool     elfFindSym(elf_t *p_elf, const char *name, uint32_t *p_value);

/* SHF_ALLOC 인 섹션을 전부 delta 만큼 옮겨 콜백으로 흘린다.
   skip_name 은 건너뛴다 (.FLM 의 DevDscr 은 호스트용이라 타깃에 안 올린다).
   p_min/p_max 에 실제 로드 범위를 돌려준다. */
bool     elfLoadSections(elf_t *p_elf, int32_t delta, const char *skip_name,
                         elf_load_cb_t cb, void *ctx,
                         uint32_t *p_min, uint32_t *p_max);

// PT_LOAD 세그먼트를 p_vaddr 그대로 올린다 (.stldr 은 재배치하면 안 된다).
bool     elfLoadSegments(elf_t *p_elf, elf_load_cb_t cb, void *ctx,
                         uint32_t *p_min, uint32_t *p_max);

// SHF_ALLOC 섹션 중 가장 낮은 주소. 재배치 delta 계산에 쓴다.
bool     elfGetAllocBase(elf_t *p_elf, const char *skip_name, uint32_t *p_base);


#endif


#ifdef __cplusplus
}
#endif

#endif
