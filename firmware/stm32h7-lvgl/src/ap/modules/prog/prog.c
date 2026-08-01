/*
 * prog.c
 *
 *  오프라인 다운로더 모듈
 */

#include "prog/prog.h"
#include "prog/prog_elf.h"
#include "prog/prog_flm.h"
#include "swd.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"
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

  // ---- .FLM 플래시 알고리즘 -------------------------------------------

  if (args->argc == 3 && args->isStr(0, "flm") == true && args->isStr(1, "info") == true)
  {
    static flm_t flm;

    if (flmOpen(&flm, args->getStr(2)) == false)
    {
      cliPrintf("FLM 열기 실패 (심볼이나 DevDscr 이 없다)\n");
    }
    else
    {
      cliPrintf("DevName  : %s\n", flm.dev.name);
      cliPrintf("Vers     : 0x%04X   DevType : %d %s\n", flm.dev.vers, flm.dev.dev_type,
                (flm.dev.dev_type == FLM_DEV_ONCHIP) ? "(ONCHIP)" :
                (flm.dev.dev_type == FLM_DEV_EXTSPI) ? "(EXTSPI)" : "");
      cliPrintf("DevAdr   : 0x%08X   szDev : %d KB\n", flm.dev.dev_adr, (int)(flm.dev.sz_dev/1024));
      cliPrintf("szPage   : %d B      valEmpty : 0x%02X\n", (int)flm.dev.sz_page, flm.dev.val_empty);
      cliPrintf("timeout  : prog %d ms, erase %d ms\n", (int)flm.dev.to_prog, (int)flm.dev.to_erase);
      cliPrintf("sectors  :\n");
      for (uint32_t i = 0; i < flm.dev.sector_cnt; i++)
      {
        uint32_t nxt = (i + 1 < flm.dev.sector_cnt) ? flm.dev.sector[i+1].addr : flm.dev.sz_dev;
        uint32_t cnt = (nxt - flm.dev.sector[i].addr) / flm.dev.sector[i].size;

        cliPrintf("   %6d KB x %-2d  @ 0x%08X\n", (int)(flm.dev.sector[i].size/1024), (int)cnt,
                  flm.dev.dev_adr + flm.dev.sector[i].addr);
      }
      cliPrintf("symbols  : Init %08X UnInit %08X\n", flm.fn_init, flm.fn_uninit);
      cliPrintf("           EraseSector %08X EraseChip %08X ProgramPage %08X\n",
                flm.fn_erase_sector, flm.fn_erase_chip, flm.fn_program);
      flmClose(&flm);
    }
    ret = true;
  }

  /* 한 페이지만 지우고 굽고 되읽어 본다. 알고리즘 배관 전체를 검증하는
     최소 단위다. flash_addr 은 반드시 지워도 되는 곳이어야 한다. */
  if (args->argc == 5 && args->isStr(0, "flm") == true && args->isStr(1, "test") == true)
  {
    static flm_t    flm;
    static uint8_t  page[1024];
    static uint32_t rd[256];
    char           *path  = args->getStr(2);
    uint32_t        ram   = (uint32_t)args->getData(3);
    uint32_t        flash = (uint32_t)args->getData(4);
    swd_err_t       err;
    uint32_t        t0;

    if (flmOpen(&flm, path) == false)
    {
      cliPrintf("FLM 열기 실패\n");
      ret = true;
    }
    else
    {
      uint32_t sec_base = flmSectorBase(&flm, flash);
      uint32_t sec_size = flmSectorSize(&flm, flash);
      uint32_t page_len = (flm.dev.sz_page > sizeof(page)) ? sizeof(page) : flm.dev.sz_page;
      bool     fail = false;

      cliPrintf("algo     : %s\n", flm.dev.name);
      cliPrintf("target   : 0x%08X  (섹터 0x%08X, %d KB)\n",
                flash, sec_base, (int)(sec_size/1024));

      // 1) 코어를 세우고 알고리즘을 올린다
      if (swdCmHalt() != SWD_OK)
      {
        cliPrintf("halt 실패\n"); fail = true;
      }
      if (!fail && (err = flmLoad(&flm, ram, 0x8000)) != SWD_OK)
      {
        cliPrintf("load 실패 : %s\n", swdErrStr(err)); fail = true;
      }
      if (!fail)
      {
        cliPrintf("arena    : code 0x%08X  static 0x%08X\n", flm.algo.code_addr, flm.algo.static_base);
        cliPrintf("           stack 0x%08X  buf 0x%08X\n", flm.algo.stack_top, flm.buf_addr);
      }

      // 2) 지우기
      if (!fail)
      {
        t0 = millis();
        err = flmInit(&flm, flm.dev.dev_adr, 8000000, FLM_FNC_ERASE);
        cliPrintf("Init(1)  : %s (%d ms)\n", swdErrStr(err), (int)(millis()-t0));
        if (err != SWD_OK) fail = true;
      }
      if (!fail)
      {
        t0 = millis();
        err = flmEraseSector(&flm, sec_base);
        cliPrintf("Erase    : %s (%d ms)\n", swdErrStr(err), (int)(millis()-t0));
        if (err != SWD_OK) fail = true;
      }
      if (!fail)
      {
        flmUnInit(&flm, FLM_FNC_ERASE);
        swdMemReadBlock(flash, rd, 8);
        cliPrintf("blank    : %08X %08X %08X %08X -> %s\n", rd[0], rd[1], rd[2], rd[3],
                  (rd[0]==0xFFFFFFFF && rd[1]==0xFFFFFFFF) ? "OK" : "FAIL");
        if (rd[0] != 0xFFFFFFFF) fail = true;
      }

      // 3) 패턴을 굽고 되읽는다
      if (!fail)
      {
        for (uint32_t i = 0; i < page_len; i++) page[i] = (uint8_t)(0xA5 ^ i);

        t0 = millis();
        err = flmInit(&flm, flm.dev.dev_adr, 8000000, FLM_FNC_PROGRAM);
        if (err == SWD_OK) err = flmProgramPage(&flm, flash, page, page_len);
        cliPrintf("Program  : %s (%d B, %d ms)\n", swdErrStr(err), (int)page_len, (int)(millis()-t0));
        flmUnInit(&flm, FLM_FNC_PROGRAM);
        if (err != SWD_OK) fail = true;
      }
      if (!fail)
      {
        uint32_t bad = 0;

        swdMemReadBlock(flash, rd, page_len / 4);
        for (uint32_t i = 0; i < page_len; i++)
        {
          if (((uint8_t *)rd)[i] != page[i]) bad++;
        }
        cliPrintf("verify   : %08X %08X %08X %08X\n", rd[0], rd[1], rd[2], rd[3]);
        cliPrintf("           불일치 %d / %d  -> %s\n", (int)bad, (int)page_len,
                  bad ? "FAIL" : "PASS");
      }

      flmClose(&flm);
      ret = true;
    }
  }

  /* 파일 하나를 통째로 굽는다. 다운로더의 핵심 경로다. */
  if (args->argc == 5 && args->isStr(0, "write") == true)
  {
    static flm_t flm;
    char     *algo  = args->getStr(1);
    char     *img   = args->getStr(2);
    uint32_t  ram   = (uint32_t)args->getData(3);
    uint32_t  flash = (uint32_t)args->getData(4);
    swd_err_t err;
    uint32_t  written = 0, bad = 0, t0;

    if (flmOpen(&flm, algo) == false)
    {
      cliPrintf("FLM 열기 실패 : %s\n", algo);
      ret = true;
    }
    else if (flmIsInRange(&flm, flash) == false)
    {
      cliPrintf("주소가 플래시 범위 밖이다 : 0x%08X (0x%08X ~ 0x%08X)\n",
                flash, flm.dev.dev_adr, flm.dev.dev_adr + flm.dev.sz_dev - 1);
      flmClose(&flm);
      ret = true;
    }
    else
    {
      cliPrintf("algo   : %s\n", flm.dev.name);
      cliPrintf("image  : %s -> 0x%08X\n", img, flash);

      if (swdCmHalt() != SWD_OK)
      {
        cliPrintf("halt 실패\n");
      }
      else if ((err = flmLoad(&flm, ram, 0x8000)) != SWD_OK)
      {
        cliPrintf("algo load 실패 : %s\n", swdErrStr(err));
      }
      else
      {
        t0  = millis();
        err = flmWriteFile(&flm, img, flash, NULL, NULL, &written);
        cliPrintf("write  : %s  %d bytes, %d ms\n",
                  swdErrStr(err), (int)written, (int)(millis() - t0));

        if (err == SWD_OK)
        {
          t0  = millis();
          err = flmVerifyFile(&flm, img, flash, NULL, NULL, &bad);
          cliPrintf("verify : %s  불일치 %d, %d ms  -> %s\n",
                    swdErrStr(err), (int)bad, (int)(millis() - t0),
                    (err == SWD_OK && bad == 0) ? "PASS" : "FAIL");
        }
      }
      flmClose(&flm);
      ret = true;
    }
  }

  if (ret != true)
  {
    cliPrintf("prog info\n");
    cliPrintf("prog elf info <path>        ELF 섹션/세그먼트/심볼 덤프\n");
    cliPrintf("prog elf load <path> <ram>  타깃 RAM 으로 재배치 로드\n");
    cliPrintf("prog flm info <path>        FlashDevice + 심볼\n");
    cliPrintf("prog flm test <path> <ram> <flash>  한 페이지 지우기/굽기/검증\n");
    cliPrintf("prog write <algo> <img> <ram> <flash>  파일 통째로 굽고 검증\n");
  }
}
#endif

#endif
