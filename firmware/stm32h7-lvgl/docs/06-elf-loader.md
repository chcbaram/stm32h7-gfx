# 6. ELF 파서를 펌웨어에 넣기

> SWD 오프라인 다운로더 만들기 — [전체 목차](README.md)

[5편](05-flash-algorithm.md)에서 `.FLM` 파일을 SD카드에 올리고, PC에서 구조를
확인했다. 이제 **펌웨어가 그 파일을 직접 읽어 타깃 RAM에 올릴 차례**다.

---

## 1. 통째로 읽지 않는다

가장 쉬운 방법은 파일을 통째로 RAM에 올리고 포인터로 헤집는 것이다. `.FLM`은
13KB니까 충분히 가능하다.

그런데 `.stldr`(ST External Loader)은 수백 KB짜리도 있다. 지금 편하자고 통째로
읽는 코드를 짜면 나중에 전부 고쳐야 한다. **처음부터 스트리밍으로 짰다.**

```c
bool elfRead(elf_t *p_elf, uint32_t offset, void *p_buf, uint32_t len)
{
  f_lseek(&p_elf->file, offset);
  f_read(&p_elf->file, p_buf, len, &br);
  return (br == len);
}
```

섹션 헤더 하나가 40바이트, 심볼 하나가 16바이트다. 필요할 때마다 그 자리만 읽는다.
읽기 횟수는 늘지만 SD카드 읽기가 병목이 될 일은 없다.

**버퍼는 32바이트 정렬 + 32바이트 배수**로 잡았다. 이 보드의 `sd.c`가 DMA 읽기 뒤에
`SCB_InvalidateDCache_by_Addr`를 부르는데, 정렬이 안 맞으면 이웃 변수의 dirty
캐시라인까지 날아간다. [1편에서 MODER 캐시로 SD·LCD·QSPI를 죽였던 것](01-swd-transport.md)과
같은 부류의 함정이다.

## 2. 무엇을 읽어야 하나

ELF32의 구조는 단순하다. 헤더에 "섹션 표가 어디 있는지"가 있고, 섹션 표에 각 섹션의
위치와 크기가 있다.

```
ELF 헤더 (52 B)
  e_shoff      섹션 표의 파일 오프셋
  e_shnum      섹션 개수
  e_shstrndx   섹션 이름 문자열이 든 섹션의 번호

섹션 헤더 (40 B × e_shnum)
  sh_name      이름 (문자열 테이블 안의 오프셋)
  sh_type      PROGBITS(내용 있음) / NOBITS(.bss) / SYMTAB / STRTAB
  sh_flags     ALLOC(메모리에 올라감) / EXECINSTR / WRITE
  sh_addr      링크된 주소
  sh_offset    파일 안 위치
  sh_size      크기
```

우리가 필요한 건 셋뿐이다.

- **`SHF_ALLOC`이 붙은 섹션** — 타깃 메모리에 올라가야 하는 것들
- **`.symtab`** — `Init`, `EraseSector`, `ProgramPage` 의 주소
- **`DevDscr`** — `FlashDevice` 구조체 (호스트만 읽는다)

## 3. 재배치

`.FLM`의 `PrgCode`는 주소 `0x00000000`에 링크되어 있다. 타깃 RAM은 `0x20000000`대라
그대로 올릴 수 없다.

`.FLM`은 ROPI(위치 독립 코드)로 빌드되어 있어서 내부 배치만 유지하면 어디서든 돈다.
그래서 이렇게 한다.

```c
// ALLOC 섹션 중 가장 낮은 주소를 찾고
elfGetAllocBase(&elf, "DevDscr", &base);      // → 0x00000000

// 올릴 자리와의 차이를 구해서
delta = ram_addr - base;                       // → +0x20001000

// 모든 섹션 주소에 더한다
addr = sec.addr + delta;
```

`DevDscr`을 base 계산에서 빼는 게 중요하다. 그건 타깃에 안 올리는 섹션이라
포함시키면 배치가 어긋난다.

`.stldr`은 정반대다. 절대 주소로 링크되어 있어서 **재배치하면 안 되고** `PT_LOAD`
세그먼트를 `p_vaddr` 그대로 올려야 한다. 그래서 함수를 둘로 나눠 뒀다.

```c
elfLoadSections(&elf, delta, "DevDscr", cb, ctx, &lo, &hi);  // .FLM
elfLoadSegments(&elf, cb, ctx, &lo, &hi);                    // .stldr
```

## 4. 콜백으로 흘려보낸다

파서가 타깃에 직접 쓰지 않는다. 조각을 콜백으로 넘기고, 어디에 어떻게 쓸지는
호출부가 정한다. 나중에 "타깃에 쓰는 대신 CRC만 계산" 같은 용도로 재사용할 수 있다.

```c
typedef bool (*elf_load_cb_t)(uint32_t addr, const uint8_t *p_data,
                              uint32_t len, void *ctx);
```

`p_data`가 `NULL`이면 `.bss`처럼 **파일에 내용이 없으니 그만큼 0으로 채우라**는 뜻이다.

### 여기서 버그를 하나 심을 뻔했다

처음엔 콜백을 이렇게 썼다.

```c
swdMemWriteBlock(addr, (const uint32_t *)p_data, len / 4);   // 위험
```

**섹션 크기도 주소도 4의 배수라는 보장이 없다.** 우리 `.FLM`은 우연히 `PrgCode`가
328바이트, `PrgData`가 4바이트라 둘 다 4의 배수여서 그냥 통과한다. 하지만 `.stldr`
이나 다른 벤더 파일에서 반드시 물린다. 게다가 `uint8_t*`를 `uint32_t*`로 캐스팅하는
것도 정렬이 안 맞으면 문제가 된다.

앞뒤 자투리는 8비트 접근으로 처리하고 가운데 정렬된 구간만 블록 전송하도록 고쳤다.
바이트를 정렬된 버퍼로 옮겨 담은 뒤 쓴다.

## 5. 실기 확인

보드에서 `.FLM`을 훑어봤다.

```
cli# prog elf info /prog/loaders/STM32F4xx_512.FLM
file    : /prog/loaders/STM32F4xx_512.FLM  (13888 bytes)
type    : 2 (1=REL 2=EXEC 3=DYN)   machine: 40 (40=ARM)
sections: 16   segments: 2

--- ALLOC 섹션 ---
  PrgCode      PROGBITS addr 0x00000000 size 0x000148 [AX]
  PrgData      PROGBITS addr 0x00000148 size 0x000004 [WA]
  DevDscr      PROGBITS addr 0x0000014C size 0x0010A0 [A]

--- 심볼 ---
  Init           0x0000001D (Thumb)
  UnInit         0x0000004F (Thumb)
  EraseChip      0x0000005D (Thumb)
  EraseSector    0x00000089 (Thumb)
  ProgramPage    0x000000D5 (Thumb)
  FlashDevice    0x0000014C

alloc base : 0x00000000  (재배치 기준)
```

**[5편에서 PC로 확인한 값과 한 글자도 다르지 않다.** 서로 다른 언어로 짠 두 파서가
같은 답을 내놓으면 둘 다 맞을 가능성이 크다.

이제 실제로 올려본다.

```
cli# swd rsthalt
halt at  : PC 0x08033E70  SP 0x20020000   reason : VCATCH

cli# prog elf load /prog/loaders/STM32F4xx_512.FLM 0x20001000
base    : 0x00000000 -> 0x20001000  (delta +536875008)
loaded  : 0x20001000 ~ 0x2000114C  (332 bytes, 13 ms)

cli# swd md 0x20001000 8
20001000 : 0E000300 D3022820 1D000940 28104770
20001010 : 0900D302 47701CC0 47700880 49424843
```

파일에서 읽은 `PrgCode`의 첫 32바이트와 비교하면:

```
파일 : 0E000300 D3022820 1D000940 28104770 · 0900D302 47701CC0 47700880 49424843
타깃 : 0E000300 D3022820 1D000940 28104770 · 0900D302 47701CC0 47700880 49424843
```

**완전히 일치한다.** 그리고 로드된 크기가 332바이트 — `PrgCode` 328 + `PrgData` 4 다.
`DevDscr` 4,256바이트는 제대로 건너뛰었다.

## 6. 다음

타깃 RAM에 플래시 알고리즘이 올라가 있고, [4편](04-algo-runner.md)에서 만든 러너가
함수를 호출할 수 있다. 남은 건 둘을 연결하는 것뿐이다.

- `DevDscr`에서 `FlashDevice`를 읽어 섹터 배치와 타임아웃을 얻고
- 심볼 주소에 `delta`를 더해 진짜 진입점을 구하고
- `Init(addr, clk, fnc)` → `EraseSector(addr)` → `ProgramPage(addr, sz, buf)` 순으로 호출

다음 편이 **첫 굽기**다.
