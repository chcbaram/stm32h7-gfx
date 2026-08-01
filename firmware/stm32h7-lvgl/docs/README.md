# STM32로 STM32를 굽는다 — SWD 오프라인 다운로더

PC도 ST-LINK도 없이, 보드 하나로 다른 MCU에 펌웨어를 굽는 장비를 만든다.
SD카드에서 펌웨어를 골라 타깃에 쓰고 검증하는 것이 최종 목표다.

- 보드: STM32H723 (550MHz, 480×480 터치 LCD, LVGL)
- 타깃: STM32F411
- 배선: **SWCLK = PE3, SWDIO = PC10. 이게 전부다.**

---

## 계층 구조

![계층 구조](images/swd-layers.svg)

**아래 한 층만 이 보드에 묶여 있고 나머지는 ARM 표준이다.** `swd.c`는 PE3/PC10과
STM32의 GPIO 레지스터를 직접 다루지만, 그 위의 DP/AP·디버그 코어·알고리즘 러너는
ARM이 정한 규격이라 ST든 Nordic이든 NXP든 같은 코드가 돈다.

이게 나중에 "다른 제조사 MCU 지원"이 거의 공짜가 되는 이유다. 벤더가 늘어도
바뀌는 건 디바이스 ID를 어디서 읽느냐 정도고, 플래시를 굽는 절차 자체는
`.FLM` 파일 안에 들어있다.

---

## 목차

| 편 | 내용 | 상태 |
|---|---|---|
| [1. 선 두 가닥으로 남의 MCU와 대화하기](01-swd-transport.md) | SWD 프로토콜, GPIO 비트뱅잉, turnaround, MODER 캐싱, 내장 로직 애널라이저로 간헐적 비트 오류 추적 | 완료 |
| [2. 타깃 메모리 읽고 쓰기](02-dap-memory.md) | ADIv5 DP/AP, MEM-AP, posted read 와 1KB TAR 랩 함정 | 완료 |
| [3. 코어를 세우고 레지스터 만지기](03-debug-core.md) | halt / run / step, nRST 없이 리셋 벡터에서 정지 | 완료 |
| [4. 타깃 RAM에서 내 코드 실행시키기](04-algo-runner.md) | 함수 호출 규약을 SWD로 흉내, 4바이트 블롭으로 엔진 검증 | 완료 |
| [5. 플래시 알고리즘을 어디서 구하나](05-flash-algorithm.md) | CMSIS-Pack `.FLM`, 포맷 검증, SD카드 배치 | 완료 |
| [6. ELF 파서를 펌웨어에 넣기](06-elf-loader.md) | 스트리밍 ELF32 파서, 재배치, 타깃 RAM 로드 | 완료 |
| [7. 첫 굽기 — 그리고 타깃을 한 번 죽였다](07-first-burn.md) | `FlashDevice` 바인딩, 파일 굽기, 링크 복구 | 완료 |
| [8. 얼마나 빠른가](08-performance.md) | pyOCD·ST 도구와 비교, 이중 버퍼링, 병목 분석 | 완료 |
| [9. 펌웨어 파일 세 가지](09-image-format.md) | `.bin` / `.elf` / Intel HEX, 주소는 어디서 오나 | 완료 |
| 10. 설정 파일과 잡 제어 | 디바이스 DB, 매니페스트 | 예정 |
| 11. GUI | LVGL 앱, 펌웨어 선택, 진행률 | 예정 |

---

## 지금까지 확인된 것

```
cli# swd core
CPUID    : 0x410FC241  ARM Cortex-M4 r0p1
DEV ID   : 0x10006431 @ 0xE0042000  dev_id 0x431  ST DBGMCU

cli# swd rsthalt
halt at  : PC 0x08033E70  SP 0x20020000   reason : VCATCH

cli# swd bench 0x20000000 32
read 32 KB in 41 ms -> 780 KB/s

cli# prog write /prog/loaders/STM32F4xx_512.FLM /prog/fw/f411_lcd/app.elf 0x20001000
image  : /prog/fw/f411_lcd/app.elf (elf, 주소는 파일 안에)
write  : OK  0x08000000, 299048 bytes, 8799 ms
verify : OK  불일치 0, 1556 ms  -> PASS
```

- SWD 링크, 타깃 메모리 읽기·쓰기 (실용 상한 약 3.5 MHz, 780 KB/s)
- 코어 halt / run / 싱글스텝 / 레지스터 읽기·쓰기
- nRST 없이 리셋 벡터에서 정확히 정지
- 타깃 RAM 에서 임의 함수 호출
- `.FLM` 파싱과 재배치 로드
- **292KB 펌웨어를 굽고 검증하고 부팅까지** (10.2초, pyOCD 의 2.1배 속도)
- `.bin` / `.elf` / Intel HEX — 확장자가 아니라 내용으로 판별하고, `.bin` 만 주소를 묻는다
- 순간적인 비트 오류를 스스로 복구 (3.5MHz 에서 52회 발생, 52회 복구)
- 파형을 직접 보고 프로토콜을 디코드하는 내장 도구

---

## SD카드 배치

```
/prog/
  mcu/       디바이스 정의. 이 폴더의 *.txt 를 전부 읽는다
  loaders/   플래시 알고리즘 (.FLM / .stldr)
  fw/        프로젝트별 폴더. fw.txt 와 이미지
```

경로는 `hw_def.h` 의 `HW_SWD_SD_ROOT` 하나만 바꾸면 전부 따라온다.
