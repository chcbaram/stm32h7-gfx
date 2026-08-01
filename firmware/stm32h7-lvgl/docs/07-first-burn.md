# 7. 첫 굽기 — 그리고 타깃을 한 번 죽였다

> SWD 오프라인 다운로더 만들기 — [전체 목차](README.md)

[6편](06-elf-loader.md)에서 `.FLM` 을 타깃 RAM 에 올렸다. 이제 실제로 플래시를
지우고 쓸 차례다.

---

## 1. FlashDevice 를 읽는다

`.FLM` 의 `DevDscr` 섹션에 `FlashDevice` 구조체가 들어 있다. 타깃에는 안 올리고
호스트가 읽는 메타데이터다.

```
off  size
  0    2   Vers
  2  128   DevName
130    2   DevType
132    4   DevAdr
136    4   szDev
140    4   szPage
148    1   valEmpty
152    4   toProg   (ms)
156    4   toErase  (ms)
160  8×N   { szSector, AddrSector }   0xFFFFFFFF 쌍으로 끝
```

섹터 배열에 함정이 하나 있다. **`AddrSector` 는 절대 주소가 아니라 `DevAdr` 기준
상대 오프셋**이고, **그 크기는 다음 항목이 나올 때까지 유지된다.** 그래서 세 줄이
여덟 개의 섹터를 뜻한다.

```
cli# prog flm info /prog/loaders/STM32F4xx_512.FLM
DevName  : STM32F4xx 512kB Flash
DevAdr   : 0x08000000   szDev : 512 KB
szPage   : 1024 B       valEmpty : 0xFF
timeout  : prog 100 ms, erase 6000 ms
sectors  :
     16 KB x 4   @ 0x08000000
     64 KB x 1   @ 0x08010000
    128 KB x 3   @ 0x08020000
```

## 2. 성공 코드가 반대다

`.FLM` 은 성공 시 **0** 을 반환한다. `.stldr` 은 **1** 이다.

이 극성을 호출부에 흩어놓으면 나중에 `.stldr` 을 붙일 때 `if (ret == 0)` 을 전부
찾아 뒤집어야 한다. 한 군데라도 빠뜨리면 "성공했는데 실패로 처리" 하거나 그 반대가
된다. 그래서 호출 래퍼 하나에 가뒀다.

```c
static swd_err_t flmCall(...)
{
  err = swdAlgoCall(&p_flm->algo, pc, r0, r1, r2, r3, timeout_ms, &ret);
  if (err != SWD_OK) return err;
  if (ret != 0)      return SWD_ERR_FAULT;   // .stldr 이면 여기만 바꾼다
  return SWD_OK;
}
```

## 3. 그리고 타깃을 죽였다

첫 시험은 **비어 있는 것을 확인한 섹터 7**(`0x08060000`)에서 하기로 했다. 미리
16개 지점을 찍어 전부 `0xFF` 인 것도 확인했다.

```
cli# prog flm test /prog/loaders/STM32F4xx_512.FLM 0x20001000 0x08060000
prog flm test /prog/loaders/STM32F4xx_512.FLM 0x20001000 0x080
target   : 0x00000080  (섹터 0x00000080, 0 KB)
Erase    : OK (401 ms)
Program  : OK (1024 B, 32 ms)
verify   : 불일치 0 / 1024  -> PASS
```

전부 PASS 인데 `target` 이 이상하다. **명령이 잘렸다.**

`HW_CLI_LINE_BUF_MAX` 가 64자인데 입력이 67자였다. `0x08060000` 이 `0x080` 이
되었고, F411 은 BOOT0=0 일 때 플래시가 `0x00000000` 에도 매핑되므로 `0x80` 은
곧 `0x08000080` 이다.

**섹터 0 을 지우고 벡터 테이블 자리에 테스트 패턴을 썼다.**

```
cli# swd md 0x08000000 4
08000000 : FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF     ← 벡터 테이블이 사라졌다
cli# swd md 0x08000080 4
08000080 : A6A7A4A5 A2A3A0A1 AEAFACAD AAABA8A9     ← 테스트 패턴
```

### 원인은 하나가 아니었다

1. **CLI 줄 버퍼 64자** — 직접 원인. 128자로 늘렸다.
2. **범위 검사가 없었다** — 더 근본적인 문제다. `flmSectorBase()` 가 플래시 범위
   밖 주소를 받으면 **그 주소를 그대로 돌려주고 있었다.** 잘린 인자가 아무 저항
   없이 통과한 이유다.

```c
bool flmIsInRange(flm_t *p_flm, uint32_t addr)
{
  return (addr >= p_flm->dev.dev_adr) &&
         (addr <  p_flm->dev.dev_adr + p_flm->dev.sz_dev);
}
```

지우기와 굽기 진입점에서 이걸 먼저 본다. **지우는 명령은 인자를 의심하는 게
기본**이라는 걸 비싸게 배웠다.

### 복구

다행히 복구는 어렵지 않았다. SWD 는 타깃 펌웨어와 무관하게 동작하고, 원본 `.bin`
도 있었다. 그리고 **복구 수단을 만드는 것이 원래 다음 할 일**이었다.

## 4. 파일 하나를 통째로 굽기

```
1. 파일이 걸치는 섹터를 전부 지운다 (같은 섹터를 두 번 지우지 않게 섹터 시작으로 건너뛴다)
2. Init(DevAdr, clk, PROGRAM)
3. 페이지 단위로 ProgramPage 반복. 마지막 페이지가 짧으면 valEmpty 로 채운다
4. UnInit(PROGRAM)
5. 되읽어 파일과 비교
```

첫 전체 실행 결과다.

```
cli# prog write /prog/loaders/STM32F4xx_512.FLM /prog/fw/f411_lcd/app.bin 0x20001000 0x08000000
algo   : STM32F4xx 512kB Flash
write  : OK  299048 bytes, 15109 ms
verify : NO-RESP (타깃 무응답)  -> FAIL
```

굽기는 됐는데 검증이 267ms 만에 죽었다.

## 5. 순간적인 오류 하나가 전체를 죽인다

[1편](01-swd-transport.md)에서 점퍼선의 비트 오류율이 0 이 아니라는 걸 이미
알고 있었다. 292KB 를 되읽으면 수만 번의 트랜잭션이 오가고, **그중 한 번만 깨져도
작업 전체가 중단**된다.

계획에는 "프로토콜 오류 시 line reset 후 재시도" 라고 적어뒀는데 구현하지
않았다. 넣으면서 두 가지를 배웠다.

### 재시도는 트랜잭션 단위로 하면 안 된다

블록 전송 중에는 TAR(주소 레지스터)이 이미 자동 증가해 있다. 실패한 전송만 다시
보내면 **엉뚱한 주소를 읽는다.** 조용히 틀린 데이터를 받는 것이라 오히려 더 나쁘다.

그래서 **청크 단위로, CSW/TAR 부터 다시 세워서** 재시도한다.

### line reset 은 파워업까지 지운다

처음 만든 복구 함수는 이랬다.

```c
swdLineReset();
swdIdle(8);
swdTransfer(0, 1, SWD_DP_DPIDR, &id);   // reset state 벗어나기
```

그런데도 복구가 안 됐다. **line reset 은 DP 를 리셋 상태로 되돌리고, 그러면
`CTRL/STAT` 의 `CDBGPWRUPREQ` 도 지워진다.** 재동기는 되는데 그 뒤 AP 접근이
전부 실패하니 복구가 아무 의미가 없었다.

```c
is_powered = false;
return swdDapPowerUp();     // 이게 빠져 있었다
```

이 버그를 한동안 몰랐던 이유가 있다. 952 kHz 로 돌린 첫 실행은 오류가 **0 건**
이라 복구 코드가 한 번도 실행되지 않았다. 3.5 MHz 로 올려서 오류를 일부러 만들고
나서야 드러났다.

### 통계를 붙이니 상황이 보인다

```
cli# swd stat
link err   : 52
recover    : ok 52 / fail 0
chunk retry: ok 43 / 최종실패 0
```

3.5 MHz 에서 292KB 를 굽는 동안 **52번 깨졌고 52번 다 복구했다.** 최종 결과는
불일치 0. 배선이 완벽하지 않아도 작업이 끝난다.

## 6. 타깃이 살아났다

```
cli# prog write ... 0x08000000
write  : OK  299048 bytes, 15109 ms
verify : OK  불일치 0, 5642 ms  -> PASS

cli# swd sysreset
cli# swd detach
cli# swd halt
PC = 0x080232A8   SP = 0x2001FFC8   LR = 0x080023A9
```

PC 가 리셋 벡터가 아니라 코드 깊숙한 곳이고, 스택도 쓰이고 있다. **우리 보드가
선 두 가닥으로 다른 STM32 에 292KB 를 굽고, 검증하고, 그게 부팅했다.**

## 다음

[8. 얼마나 빠른가](08-performance.md) — 잘 도는 건 확인했으니 이제 속도를 본다.
같은 칩에 pyOCD 와 ST 공식 도구로도 구워서 비교했고, 그 과정에서 병목에 대한 내
예상이 두 번 틀렸다.
