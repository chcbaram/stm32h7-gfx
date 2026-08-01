/*
 * prog.c
 *
 *  오프라인 다운로더 모듈
 */

#include "prog/prog.h"
#include "prog/prog_elf.h"
#include "swd.h"
#include "swd/swd_dap.h"
#include "cli.h"


#ifdef _USE_HW_SWD


#ifdef _USE_HW_CLI
static void cliProg(cli_args_t *args);
#endif


MODULE_DEF(prog){
  .name     = "prog",
  .priority = MODULE_PRI_HIGH,
  .init     = progInit,
};


// ----------------------------------------------------------------- 초기화

bool progInit(void)
{
  bool ret = true;

#ifdef _USE_HW_CLI
  cliAdd("prog", cliProg);
#endif

  logPrintf("[%s] progInit()\n", ret ? "OK" : "NG");
  logPrintf("     root : %s\n", HW_SWD_SD_ROOT);

  return ret;
}


// ----------------------------------------------------------------- CLI

#ifdef _USE_HW_CLI

/* 타깃 RAM 으로 흘려보내는 콜백. p_data 가 NULL 이면 0 으로 채우라는 뜻이다. */
typedef struct
{
  uint32_t bytes;
  uint32_t err_cnt;
} prog_load_ctx_t;

/* 섹션 크기도 주소도 4의 배수라는 보장이 없다. 앞뒤 자투리는 8비트 접근으로
   처리하고 가운데 정렬된 구간만 블록 전송한다. p_data 를 uint32_t* 로 그냥
   캐스팅하면 정렬이 깨질 수 있어서 정렬된 버퍼로 옮겨 담는다. */
static uint32_t prog_word_buf[128] __attribute__((aligned(4)));

static bool progLoadToTarget(uint32_t addr, const uint8_t *p_data, uint32_t len, void *ctx)
{
  prog_load_ctx_t *p_ctx = (prog_load_ctx_t *)ctx;
  swd_err_t        err   = SWD_OK;
  uint32_t         done  = 0;

  // .bss 처럼 내용이 없는 구간
  if (p_data == NULL)
  {
    while (len > 0 && (addr & 3))
    {
      err = swdMemWrite8(addr, 0);
      if (err != SWD_OK) goto fail;
      addr++; len--; done++;
    }
    if (len >= 4)
    {
      err = swdMemFill(addr, 0, len / 4);
      if (err != SWD_OK) goto fail;
      addr += (len & ~3UL); done += (len & ~3UL); len &= 3;
    }
    while (len > 0)
    {
      err = swdMemWrite8(addr, 0);
      if (err != SWD_OK) goto fail;
      addr++; len--; done++;
    }
    p_ctx->bytes += done;
    return true;
  }

  // 앞쪽 자투리
  while (len > 0 && (addr & 3))
  {
    err = swdMemWrite8(addr, *p_data);
    if (err != SWD_OK) goto fail;
    addr++; p_data++; len--; done++;
  }

  // 정렬된 구간
  while (len >= 4)
  {
    uint32_t n = len & ~3UL;

    if (n > sizeof(prog_word_buf)) n = sizeof(prog_word_buf);

    memcpy(prog_word_buf, p_data, n);
    err = swdMemWriteBlock(addr, prog_word_buf, n / 4);
    if (err != SWD_OK) goto fail;

    addr += n; p_data += n; len -= n; done += n;
  }

  // 뒤쪽 자투리
  while (len > 0)
  {
    err = swdMemWrite8(addr, *p_data);
    if (err != SWD_OK) goto fail;
    addr++; p_data++; len--; done++;
  }

  p_ctx->bytes += done;
  return true;

fail:
  p_ctx->bytes += done;
  p_ctx->err_cnt++;
  return false;
}

void cliProg(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("root    : %s\n", HW_SWD_SD_ROOT);
    cliPrintf("mcu     : %s\n", HW_SWD_SD_MCU);
    cliPrintf("loaders : %s\n", HW_SWD_SD_LOADERS);
    cliPrintf("fw      : %s\n", HW_SWD_SD_FW);
    ret = true;
  }

  // ELF 구조를 훑는다. PC 에서 검증한 것과 같은 값이 나와야 한다.
  if (args->argc == 2 && args->isStr(0, "elf") == true)
  {
    cliPrintf("사용법: prog elf info <path>\n");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "elf") == true && args->isStr(1, "info") == true)
  {
    elf_t     elf;
    char     *path = args->getStr(2);
    elf_sec_t sec;

    if (elfOpen(&elf, path) == false)
    {
      cliPrintf("열기 실패 또는 ELF 아님 : %s\n", path);
      ret = true;
    }
    else
    {
      static const char *sym_tbl[] = {"Init", "UnInit", "EraseChip", "EraseSector",
                                      "ProgramPage", "Verify", "BlankCheck",
                                      "FlashDevice", "StorageInfo", "Write",
                                      "SectorErase", "MassErase"};
      uint32_t base = 0;

      cliPrintf("file    : %s  (%d bytes)\n", path, (int)f_size(&elf.file));
      cliPrintf("type    : %d (1=REL 2=EXEC 3=DYN)   machine: %d (40=ARM)\n",
                elf.e_type, elf.e_machine);
      cliPrintf("sections: %d   segments: %d\n", elf.e_shnum, elf.e_phnum);

      cliPrintf("\n--- ALLOC 섹션 ---\n");
      for (uint32_t i = 0; i < elf.e_shnum; i++)
      {
        if (elfGetSec(&elf, i, &sec) == false) continue;
        if ((sec.flags & ELF_SHF_ALLOC) == 0)  continue;

        cliPrintf("  %-12s %-8s addr 0x%08X size 0x%06X [%s%s%s]\n",
                  sec.name,
                  (sec.type == ELF_SHT_PROGBITS) ? "PROGBITS" :
                  (sec.type == ELF_SHT_NOBITS)   ? "NOBITS"   : "?",
                  sec.addr, sec.size,
                  (sec.flags & ELF_SHF_WRITE)     ? "W" : "",
                  (sec.flags & ELF_SHF_ALLOC)     ? "A" : "",
                  (sec.flags & ELF_SHF_EXECINSTR) ? "X" : "");
      }

      if (elf.e_phnum > 0)
      {
        elf_phdr_t ph;

        cliPrintf("\n--- PT_LOAD 세그먼트 ---\n");
        for (uint32_t i = 0; i < elf.e_phnum; i++)
        {
          if (elfGetPhdr(&elf, i, &ph) == false) continue;
          if (ph.type != ELF_PT_LOAD) continue;
          cliPrintf("  vaddr 0x%08X  filesz 0x%06X  memsz 0x%06X\n",
                    ph.vaddr, ph.filesz, ph.memsz);
        }
      }

      cliPrintf("\n--- 심볼 ---\n");
      for (uint32_t i = 0; i < sizeof(sym_tbl)/sizeof(sym_tbl[0]); i++)
      {
        uint32_t v = 0;

        if (elfFindSym(&elf, sym_tbl[i], &v) == true)
        {
          cliPrintf("  %-14s 0x%08X %s\n", sym_tbl[i], v, (v & 1) ? "(Thumb)" : "");
        }
      }

      if (elfGetAllocBase(&elf, "DevDscr", &base) == true)
      {
        cliPrintf("\nalloc base : 0x%08X  (재배치 기준)\n", base);
      }

      elfClose(&elf);
      ret = true;
    }
  }

  // ELF 를 타깃 RAM 으로 실제 로드해 본다
  if (args->argc == 4 && args->isStr(0, "elf") == true && args->isStr(1, "load") == true)
  {
    elf_t           elf;
    char           *path = args->getStr(2);
    uint32_t        ram  = (uint32_t)args->getData(3);
    prog_load_ctx_t ctx  = {0, 0};
    uint32_t        base = 0, lo = 0, hi = 0;
    int32_t         delta;
    uint32_t        t_start;

    if (elfOpen(&elf, path) == false)
    {
      cliPrintf("열기 실패 : %s\n", path);
      ret = true;
    }
    else if (elfGetAllocBase(&elf, "DevDscr", &base) == false)
    {
      cliPrintf("ALLOC 섹션이 없다\n");
      elfClose(&elf);
      ret = true;
    }
    else
    {
      delta = (int32_t)ram - (int32_t)base;

      cliPrintf("base    : 0x%08X -> 0x%08X  (delta %+d)\n", base, ram, (int)delta);

      t_start = millis();
      if (elfLoadSections(&elf, delta, "DevDscr", progLoadToTarget, &ctx, &lo, &hi) == true)
      {
        cliPrintf("loaded  : 0x%08X ~ 0x%08X  (%d bytes, %d ms)\n",
                  lo, hi, (int)ctx.bytes, (int)(millis() - t_start));
      }
      else
      {
        cliPrintf("로드 실패 (%d bytes 진행, 오류 %d)\n", (int)ctx.bytes, (int)ctx.err_cnt);
      }
      elfClose(&elf);
      ret = true;
    }
  }

  if (ret != true)
  {
    cliPrintf("prog info\n");
    cliPrintf("prog elf info <path>        ELF 섹션/세그먼트/심볼 덤프\n");
    cliPrintf("prog elf load <path> <ram>  타깃 RAM 으로 재배치 로드\n");
  }
}
#endif

#endif
