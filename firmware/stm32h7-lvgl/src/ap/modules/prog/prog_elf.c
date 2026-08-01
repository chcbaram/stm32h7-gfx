/*
 * prog_elf.c
 *
 *  ELF32 스트리밍 리더
 *
 *  파일을 통째로 올리지 않는다. 섹션 헤더도 심볼도 필요할 때마다 그 자리만
 *  읽는다. 대신 읽기 횟수가 늘어나므로 헤더 파싱은 열 때 한 번만 한다.
 *
 *  버퍼가 32바이트 정렬 + 32바이트 배수여야 하는 게 중요하다. sd.c 가 DMA 읽기
 *  뒤에 SCB_InvalidateDCache_by_Addr 를 부르는데, 정렬이 안 맞으면 이웃 변수의
 *  dirty 캐시라인까지 날아간다.
 */

#include "prog/prog_elf.h"


#ifdef _USE_HW_SWD


#define ELF_BUF_SIZE      512       // 32 배수
#define ELF_SYM_ENTSIZE   16
#define ELF_NAME_MAX      40


static uint8_t elf_buf[ELF_BUF_SIZE] __attribute__((aligned(32)));


static uint16_t elfU16(const uint8_t *p);
static uint32_t elfU32(const uint8_t *p);
static bool     elfReadStr(elf_t *p_elf, uint32_t off, char *p_out, uint32_t max);
static bool     elfStreamRange(elf_t *p_elf, uint32_t file_off, uint32_t addr,
                               uint32_t len, elf_load_cb_t cb, void *ctx);
static bool     elfZeroRange(uint32_t addr, uint32_t len, elf_load_cb_t cb, void *ctx);


// ----------------------------------------------------------------- 열기/닫기

bool elfOpen(elf_t *p_elf, const char *path)
{
  uint8_t hdr[52];

  if (p_elf == NULL || path == NULL) return false;

  memset(p_elf, 0, sizeof(elf_t));
  p_elf->symtab_idx = 0xFFFFFFFF;

  if (f_open(&p_elf->file, path, FA_READ) != FR_OK)
  {
    return false;
  }
  p_elf->is_open = true;

  if (elfRead(p_elf, 0, hdr, sizeof(hdr)) == false)
  {
    elfClose(p_elf);
    return false;
  }

  if (hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F')
  {
    elfClose(p_elf);
    return false;
  }
  if (hdr[4] != 1 || hdr[5] != 1)      // EI_CLASS=32bit, EI_DATA=LE
  {
    elfClose(p_elf);
    return false;
  }

  p_elf->e_type      = elfU16(&hdr[16]);
  p_elf->e_machine   = elfU16(&hdr[18]);
  p_elf->e_entry     = elfU32(&hdr[24]);
  p_elf->e_phoff     = elfU32(&hdr[28]);
  p_elf->e_shoff     = elfU32(&hdr[32]);
  p_elf->e_phentsize = elfU16(&hdr[42]);
  p_elf->e_phnum     = elfU16(&hdr[44]);
  p_elf->e_shentsize = elfU16(&hdr[46]);
  p_elf->e_shnum     = elfU16(&hdr[48]);
  p_elf->e_shstrndx  = elfU16(&hdr[50]);

  // 섹션 이름 문자열 테이블 위치를 미리 잡아둔다
  if (p_elf->e_shnum > 0 && p_elf->e_shstrndx < p_elf->e_shnum)
  {
    uint8_t sh[40];
    uint32_t off = p_elf->e_shoff + (uint32_t)p_elf->e_shstrndx * p_elf->e_shentsize;

    if (elfRead(p_elf, off, sh, sizeof(sh)) == true)
    {
      p_elf->shstr_off = elfU32(&sh[16]);
    }
  }

  // 심볼 테이블 섹션을 찾아둔다
  for (uint32_t i = 0; i < p_elf->e_shnum; i++)
  {
    elf_sec_t sec;

    if (elfGetSec(p_elf, i, &sec) && sec.type == ELF_SHT_SYMTAB)
    {
      p_elf->symtab_idx = i;
      break;
    }
  }

  return true;
}

void elfClose(elf_t *p_elf)
{
  if (p_elf != NULL && p_elf->is_open)
  {
    f_close(&p_elf->file);
    p_elf->is_open = false;
  }
}

bool elfIsArm32(elf_t *p_elf)
{
  return (p_elf != NULL) && p_elf->is_open && (p_elf->e_machine == 40);
}


// ----------------------------------------------------------------- 읽기

bool elfRead(elf_t *p_elf, uint32_t offset, void *p_buf, uint32_t len)
{
  UINT br = 0;

  if (p_elf == NULL || p_elf->is_open == false) return false;

  if (f_lseek(&p_elf->file, offset) != FR_OK)      return false;
  if (f_read(&p_elf->file, p_buf, len, &br) != FR_OK) return false;

  return (br == len);
}

uint32_t elfSecCount(elf_t *p_elf)
{
  return (p_elf != NULL) ? p_elf->e_shnum : 0;
}

bool elfGetSec(elf_t *p_elf, uint32_t idx, elf_sec_t *p_sec)
{
  uint8_t  sh[40];
  uint32_t off;

  if (p_elf == NULL || p_sec == NULL || idx >= p_elf->e_shnum) return false;

  off = p_elf->e_shoff + idx * p_elf->e_shentsize;
  if (elfRead(p_elf, off, sh, sizeof(sh)) == false) return false;

  memset(p_sec, 0, sizeof(elf_sec_t));
  p_sec->type    = elfU32(&sh[4]);
  p_sec->flags   = elfU32(&sh[8]);
  p_sec->addr    = elfU32(&sh[12]);
  p_sec->offset  = elfU32(&sh[16]);
  p_sec->size    = elfU32(&sh[20]);
  p_sec->link    = elfU32(&sh[24]);
  p_sec->entsize = elfU32(&sh[36]);

  if (p_elf->shstr_off != 0)
  {
    elfReadStr(p_elf, p_elf->shstr_off + elfU32(&sh[0]), p_sec->name, ELF_SEC_NAME_MAX);
  }
  return true;
}

bool elfFindSec(elf_t *p_elf, const char *name, elf_sec_t *p_sec)
{
  elf_sec_t sec;

  if (p_elf == NULL || name == NULL) return false;

  for (uint32_t i = 0; i < p_elf->e_shnum; i++)
  {
    if (elfGetSec(p_elf, i, &sec) && strcmp(sec.name, name) == 0)
    {
      if (p_sec != NULL) *p_sec = sec;
      return true;
    }
  }
  return false;
}

bool elfGetPhdr(elf_t *p_elf, uint32_t idx, elf_phdr_t *p_phdr)
{
  uint8_t ph[32];

  if (p_elf == NULL || p_phdr == NULL || idx >= p_elf->e_phnum) return false;

  if (elfRead(p_elf, p_elf->e_phoff + idx * p_elf->e_phentsize, ph, sizeof(ph)) == false)
  {
    return false;
  }

  p_phdr->type   = elfU32(&ph[0]);
  p_phdr->offset = elfU32(&ph[4]);
  p_phdr->vaddr  = elfU32(&ph[8]);
  p_phdr->filesz = elfU32(&ph[16]);
  p_phdr->memsz  = elfU32(&ph[20]);
  p_phdr->flags  = elfU32(&ph[24]);

  return true;
}

bool elfFindSym(elf_t *p_elf, const char *name, uint32_t *p_value)
{
  elf_sec_t symtab;
  elf_sec_t strtab;
  uint32_t  count;

  if (p_elf == NULL || name == NULL) return false;
  if (p_elf->symtab_idx == 0xFFFFFFFF) return false;

  if (elfGetSec(p_elf, p_elf->symtab_idx, &symtab) == false) return false;
  if (symtab.entsize == 0) return false;
  if (elfGetSec(p_elf, symtab.link, &strtab) == false) return false;

  count = symtab.size / symtab.entsize;

  for (uint32_t i = 0; i < count; i++)
  {
    uint8_t sym[ELF_SYM_ENTSIZE];
    char    nm[ELF_NAME_MAX];

    if (elfRead(p_elf, symtab.offset + i * symtab.entsize, sym, ELF_SYM_ENTSIZE) == false)
    {
      return false;
    }
    if (elfReadStr(p_elf, strtab.offset + elfU32(&sym[0]), nm, ELF_NAME_MAX) == false)
    {
      continue;
    }
    if (strcmp(nm, name) == 0)
    {
      if (p_value != NULL) *p_value = elfU32(&sym[4]);
      return true;
    }
  }
  return false;
}


// ----------------------------------------------------------------- 로드

bool elfGetAllocBase(elf_t *p_elf, const char *skip_name, uint32_t *p_base)
{
  elf_sec_t sec;
  uint32_t  base = 0xFFFFFFFF;

  if (p_elf == NULL) return false;

  for (uint32_t i = 0; i < p_elf->e_shnum; i++)
  {
    if (elfGetSec(p_elf, i, &sec) == false)     continue;
    if ((sec.flags & ELF_SHF_ALLOC) == 0)       continue;
    if (sec.size == 0)                          continue;
    if (skip_name && strcmp(sec.name, skip_name) == 0) continue;

    if (sec.addr < base) base = sec.addr;
  }

  if (base == 0xFFFFFFFF) return false;
  if (p_base != NULL) *p_base = base;
  return true;
}

bool elfLoadSections(elf_t *p_elf, int32_t delta, const char *skip_name,
                     elf_load_cb_t cb, void *ctx,
                     uint32_t *p_min, uint32_t *p_max)
{
  elf_sec_t sec;
  uint32_t  lo = 0xFFFFFFFF;
  uint32_t  hi = 0;

  if (p_elf == NULL || cb == NULL) return false;

  for (uint32_t i = 0; i < p_elf->e_shnum; i++)
  {
    uint32_t addr;

    if (elfGetSec(p_elf, i, &sec) == false)     continue;
    if ((sec.flags & ELF_SHF_ALLOC) == 0)       continue;
    if (sec.size == 0)                          continue;
    if (skip_name && strcmp(sec.name, skip_name) == 0) continue;

    addr = (uint32_t)((int32_t)sec.addr + delta);

    if (sec.type == ELF_SHT_NOBITS)
    {
      // .bss 는 파일에 내용이 없다. 그만큼 0 으로 채운다.
      if (elfZeroRange(addr, sec.size, cb, ctx) == false) return false;
    }
    else
    {
      if (elfStreamRange(p_elf, sec.offset, addr, sec.size, cb, ctx) == false) return false;
    }

    if (addr < lo)              lo = addr;
    if (addr + sec.size > hi)   hi = addr + sec.size;
  }

  if (lo == 0xFFFFFFFF) return false;
  if (p_min != NULL) *p_min = lo;
  if (p_max != NULL) *p_max = hi;
  return true;
}

bool elfLoadSegments(elf_t *p_elf, elf_load_cb_t cb, void *ctx,
                     uint32_t *p_min, uint32_t *p_max)
{
  elf_phdr_t ph;
  uint32_t   lo = 0xFFFFFFFF;
  uint32_t   hi = 0;

  if (p_elf == NULL || cb == NULL) return false;

  for (uint32_t i = 0; i < p_elf->e_phnum; i++)
  {
    if (elfGetPhdr(p_elf, i, &ph) == false) continue;
    if (ph.type != ELF_PT_LOAD)             continue;
    if (ph.memsz == 0)                      continue;

    if (ph.filesz > 0)
    {
      if (elfStreamRange(p_elf, ph.offset, ph.vaddr, ph.filesz, cb, ctx) == false) return false;
    }
    // memsz 가 filesz 보다 크면 그 차이가 .bss 다
    if (ph.memsz > ph.filesz)
    {
      if (elfZeroRange(ph.vaddr + ph.filesz, ph.memsz - ph.filesz, cb, ctx) == false) return false;
    }

    if (ph.vaddr < lo)              lo = ph.vaddr;
    if (ph.vaddr + ph.memsz > hi)   hi = ph.vaddr + ph.memsz;
  }

  if (lo == 0xFFFFFFFF) return false;
  if (p_min != NULL) *p_min = lo;
  if (p_max != NULL) *p_max = hi;
  return true;
}


// ----------------------------------------------------------------- 내부

uint16_t elfU16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t elfU32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 문자열 테이블에서 널 종료 문자열을 읽는다. 길이를 모르므로 조금씩 읽는다. */
bool elfReadStr(elf_t *p_elf, uint32_t off, char *p_out, uint32_t max)
{
  UINT br = 0;

  if (p_out == NULL || max == 0) return false;

  p_out[0] = 0;
  if (f_lseek(&p_elf->file, off) != FR_OK) return false;
  if (f_read(&p_elf->file, p_out, max - 1, &br) != FR_OK) return false;

  p_out[(br < max - 1) ? br : (max - 1)] = 0;
  return true;
}

/* 파일의 한 구간을 조각내어 콜백으로 흘린다. 통째로 버퍼링하지 않는다. */
bool elfStreamRange(elf_t *p_elf, uint32_t file_off, uint32_t addr,
                    uint32_t len, elf_load_cb_t cb, void *ctx)
{
  if (f_lseek(&p_elf->file, file_off) != FR_OK) return false;

  while (len > 0)
  {
    uint32_t n = (len > ELF_BUF_SIZE) ? ELF_BUF_SIZE : len;
    UINT     br = 0;

    if (f_read(&p_elf->file, elf_buf, n, &br) != FR_OK || br != n) return false;
    if (cb(addr, elf_buf, n, ctx) == false) return false;

    addr += n;
    len  -= n;
  }
  return true;
}

bool elfZeroRange(uint32_t addr, uint32_t len, elf_load_cb_t cb, void *ctx)
{
  while (len > 0)
  {
    uint32_t n = (len > ELF_BUF_SIZE) ? ELF_BUF_SIZE : len;

    if (cb(addr, NULL, n, ctx) == false) return false;

    addr += n;
    len  -= n;
  }
  return true;
}


#endif
