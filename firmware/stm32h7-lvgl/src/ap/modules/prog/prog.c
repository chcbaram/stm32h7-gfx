/*
 * prog.c
 *
 *  오프라인 다운로더 모듈
 */

#include "prog/prog.h"
#include "prog/prog_elf.h"
#include "prog/prog_algo.h"
#include "prog/prog_flm.h"
#include "prog/prog_stldr.h"
#include "swd.h"
#include "swd/swd_dap.h"
#include "swd/swd_cm.h"
#include "cli.h"


#ifdef _USE_HW_SWD


#ifdef _USE_HW_CLI
static void cliProg(cli_args_t *args);
static uint32_t prog_psize = ALGO_PSIZE_8;
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

  /* 소거·굽기 병렬도. .stldr 만 인자로 받고 .FLM 은 알고리즘이 정한 값을 쓴다.
     타깃 VDD 가 모자란데 큰 단위로 쓰면 플래시가 조용히 깨지므로 기본은 x8 이다.
     STM32F4 기준 x32 는 2.7V 이상, x64 는 Vpp 8~9V 가 있어야 한다. */
  if (args->argc >= 1 && args->isStr(0, "psize") == true)
  {
    const char *nm[4] = { "x8 (VDD 1.8~2.1V)", "x16 (2.1~2.7V)", "x32 (2.7~3.6V)", "x64 (Vpp 필요)" };

    if (args->argc == 2)
    {
      uint32_t v = (uint32_t)args->getData(1);

      if (v > ALGO_PSIZE_64) cliPrintf("0~3 이어야 한다\n");
      else                   prog_psize = v;
    }
    cliPrintf("psize    : %d  %s\n", (int)prog_psize, nm[prog_psize]);
    ret = true;
  }

  // ---- 플래시 알고리즘 (.FLM / .stldr) --------------------------------

  if (args->argc == 3 && args->isStr(0, "algo") == true && args->isStr(1, "info") == true)
  {
    static algo_t flm;

    if (algoOpen(&flm, args->getStr(2)) == false)
    {
      cliPrintf("알고리즘 열기 실패 (.FLM 의 DevDscr 도 .stldr 의 StorageInfo 도 없다)\n");
    }
    else
    {
      cliPrintf("kind     : %s\n", algoKindStr(&flm));
      cliPrintf("DevName  : %s\n", flm.dev.name);
      cliPrintf("DevType  : %s\n",
                (flm.dev.dev_type == ALGO_DEV_ONCHIP) ? "내부 플래시" : "외부 메모리");
      cliPrintf("DevAdr   : 0x%08X   szDev : %d KB\n", flm.dev.dev_adr, (int)(flm.dev.sz_dev/1024));
      cliPrintf("szPage   : %d B      valEmpty : 0x%02X   조각 : %d B\n",
                (int)flm.dev.sz_page, flm.dev.val_empty, (int)flm.buf_size);
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
      algoClose(&flm);
    }
    ret = true;
  }

  /* 한 페이지만 지우고 굽고 되읽어 본다. 알고리즘 배관 전체를 검증하는
     최소 단위다. flash_addr 은 반드시 지워도 되는 곳이어야 한다. */
  if (args->argc == 5 && args->isStr(0, "algo") == true && args->isStr(1, "test") == true)
  {
    static algo_t    flm;
    static uint8_t  page[1024];
    static uint32_t rd[256];
    char           *path  = args->getStr(2);
    uint32_t        ram   = (uint32_t)args->getData(3);
    uint32_t        flash = (uint32_t)args->getData(4);
    swd_err_t       err;
    uint32_t        t0;

    if (algoOpen(&flm, path) == false)
    {
      cliPrintf("FLM 열기 실패\n");
      ret = true;
    }
    else
    {
      uint32_t sec_base = algoSectorBase(&flm, flash);
      uint32_t sec_size = algoSectorSize(&flm, flash);
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
      algoSetPSize(&flm, prog_psize);
      if (!fail && (err = algoLoad(&flm, ram, 0x8000)) != SWD_OK)
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
        err = flm.ops->init(&flm, ALGO_FNC_ERASE);
        cliPrintf("Init(1)  : %s (%d ms)\n", swdErrStr(err), (int)(millis()-t0));
        if (err != SWD_OK) fail = true;
      }
      if (!fail)
      {
        t0 = millis();
        err = flm.ops->erase_sector(&flm, sec_base, algoSectorSize(&flm, sec_base));
        cliPrintf("Erase    : %s (%d ms)\n", swdErrStr(err), (int)(millis()-t0));
        if (err != SWD_OK) fail = true;
      }
      if (!fail)
      {
        flm.ops->uninit(&flm, ALGO_FNC_ERASE);
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
        err = flm.ops->init(&flm, ALGO_FNC_PROGRAM);
        if (err == SWD_OK && algoWriteMem(flm.buf_addr, page, page_len, &flm) == false) err = SWD_ERR_FAULT;
        if (err == SWD_OK) err = flm.ops->prog_start(&flm, flash, page_len, flm.buf_addr);
        if (err == SWD_OK) err = flm.ops->prog_wait(&flm, flm.dev.to_prog);
        cliPrintf("Program  : %s (%d B, %d ms)\n", swdErrStr(err), (int)page_len, (int)(millis()-t0));
        flm.ops->uninit(&flm, ALGO_FNC_PROGRAM);
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

      algoClose(&flm);
      ret = true;
    }
  }

  /* .hex 를 굽기 전에 훑어본다. 체크섬과 주소 순서도 여기서 다 확인된다. */
  if (args->argc == 3 && args->isStr(0, "hex") == true && args->isStr(1, "info") == true)
  {
    static hex_t hex;
    char *path = args->getStr(2);

    if (hexOpen(&hex, path) == false)
    {
      cliPrintf("hex 열기 실패 : %s\n", path);
      cliPrintf("  체크섬 오류거나 주소가 역행하는 파일이다\n");
    }
    else
    {
      cliPrintf("records  : %d\n", (int)hex.rec_cnt);
      cliPrintf("range    : 0x%08X ~ 0x%08X  (%d bytes)\n",
                hex.lo, hex.hi - 1, (int)(hex.hi - hex.lo));
      cliPrintf("data     : %d bytes  (빈틈 %d)\n",
                (int)hex.data_bytes, (int)(hex.hi - hex.lo - hex.data_bytes));
      if (hex.has_entry) cliPrintf("entry    : 0x%08X\n", hex.entry);
      hexClose(&hex);
    }
    ret = true;
  }

  /* 파일 하나를 통째로 굽는다. 다운로더의 핵심 경로다.
     .elf 는 굽는 주소가 파일 안에 있으므로 flash 인자가 없고, .bin 은 필요하다. */
  if ((args->argc == 4 || args->argc == 5) && args->isStr(0, "write") == true)
  {
    static algo_t flm;
    char     *algo   = args->getStr(1);
    char     *img    = args->getStr(2);
    uint32_t  ram    = (uint32_t)args->getData(3);
    uint32_t  flash  = (args->argc == 5) ? (uint32_t)args->getData(4) : 0;
    bool      is_elf = elfIsElfFile(img);
    bool      is_hex = (is_elf == false) && hexIsHexFile(img);
    bool      has_addr = is_elf || is_hex;      // 주소가 파일 안에 있다
    swd_err_t err;
    uint32_t  written = 0, bad = 0, t0;

    if (has_addr == false && args->argc == 4)
    {
      cliPrintf(".bin 은 굽는 주소가 파일에 없다. flash 주소를 지정해라\n");
      ret = true;
    }
    else if (algoOpen(&flm, algo) == false)
    {
      cliPrintf("FLM 열기 실패 : %s\n", algo);
      ret = true;
    }
    else if (has_addr == false && algoIsInRange(&flm, flash) == false)
    {
      cliPrintf("주소가 플래시 범위 밖이다 : 0x%08X (0x%08X ~ 0x%08X)\n",
                flash, flm.dev.dev_adr, flm.dev.dev_adr + flm.dev.sz_dev - 1);
      algoClose(&flm);
      ret = true;
    }
    else
    {
      cliPrintf("algo   : %s\n", flm.dev.name);
      if (has_addr)
        cliPrintf("image  : %s (%s, 주소는 파일 안에)\n", img, is_elf ? "elf" : "hex");
      else
        cliPrintf("image  : %s -> 0x%08X\n", img, flash);

      if (swdCmHalt() != SWD_OK)
      {
        cliPrintf("halt 실패\n");
      }
      else if ((algoSetPSize(&flm, prog_psize), err = algoLoad(&flm, ram, 0x8000)) != SWD_OK)
      {
        cliPrintf("algo load 실패 : %s\n", swdErrStr(err));
      }
      else
      {
        algo_time_t tm;

        t0 = millis();
        if (is_elf)
          err = algoWriteElf(&flm, img, NULL, NULL, &written, &tm, &flash);
        else if (is_hex)
          err = algoWriteHex(&flm, img, NULL, NULL, &written, &tm, &flash);
        else
          err = algoWriteFile(&flm, img, flash, NULL, NULL, &written, &tm);

        cliPrintf("write  : %s  0x%08X, %d bytes, %d ms\n",
                  swdErrStr(err), flash, (int)written, (int)(millis() - t0));
        cliPrintf("  erase   : %5d ms\n", (int)tm.erase_ms);
        cliPrintf("  sd read : %5d ms\n", (int)tm.read_ms);
        cliPrintf("  xfer    : %5d ms   (%d 페이지)\n", (int)tm.xfer_ms, (int)tm.page_cnt);
        cliPrintf("  algo    : %5d ms\n", (int)tm.call_ms);

        if (err == SWD_OK)
        {
          t0 = millis();
          if (is_elf)
            err = algoVerifyElf(&flm, img, NULL, NULL, &bad);
          else if (is_hex)
            err = algoVerifyHex(&flm, img, NULL, NULL, &bad);
          else
            err = algoVerifyFile(&flm, img, flash, NULL, NULL, &bad);

          cliPrintf("verify : %s  불일치 %d, %d ms  -> %s\n",
                    swdErrStr(err), (int)bad, (int)(millis() - t0),
                    (err == SWD_OK && bad == 0) ? "PASS" : "FAIL");
        }
      }
      algoClose(&flm);
      ret = true;
    }
  }

  if (ret != true)
  {
    cliPrintf("prog info\n");
    cliPrintf("prog elf info <path>        ELF 섹션/세그먼트/심볼 덤프\n");
    cliPrintf("prog elf load <path> <ram>  타깃 RAM 으로 재배치 로드\n");
    cliPrintf("prog psize [0~3]            소거/굽기 병렬도 (.stldr 전용)\n");
    cliPrintf("prog algo info <path>       .FLM / .stldr 자동 판별 + 디바이스 정보\n");
    cliPrintf("prog algo test <path> <ram> <flash>  한 조각 지우기/굽기/검증\n");
    cliPrintf("prog hex info <path>        레코드 수/주소 범위/엔트리\n");
    cliPrintf("prog write <algo> <img> <ram> [flash]  파일 통째로 굽고 검증\n");
    cliPrintf("                                       .elf/.hex 는 flash 주소 생략\n");
  }
}
#endif

#endif
