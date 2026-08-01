# /prog/loaders — 플래시 알고리즘

용량이 크고 ST/ARM 배포물이라 저장소에는 폴더만 있다. 직접 채운다.

## st/ — ST 내부 플래시 (권장, 가장 빠르다)

```sh
cp /opt/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin/FlashLoader/*.stldr  <SD>/prog/loaders/st/
```
103개, 3.7 MB. 파일명이 DEV_ID(`0x431.stldr`)라 타깃에서 읽은 값으로 바로 고른다.

**소거·굽기 병렬도를 인자로 받아서 `.FLM` 보다 빠르다.** 다만 기본값은 가장
안전한 x8 이라 `prog psize 2`(x32, VDD 2.7V 이상)로 올려야 그 값이 나온다.
전압이 모자란데 큰 단위로 쓰면 플래시가 조용히 깨진다.

## ext/ — 외부 QSPI / NOR / SDRAM

```sh
cp /opt/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin/ExternalLoader/*.stldr  <SD>/prog/loaders/ext/
```
90개, 27 MB. 필요한 것만 골라 넣어도 된다 — 파일명이 `칩_보드` 라 자기 보드
것만 있으면 된다.

주의: 이쪽 `Init()` 은 `SystemInit` 을 불러 **타깃 클럭을 재설정한다.** 반드시
reset-halt 상태에서 시작해야 하고, 로더를 바꿀 때는 다시 리셋해야 한다.

## flm/ — CMSIS-Pack (벤더 중립)

ST 이외의 MCU 는 이쪽이다. Nordic·NXP·Renesas·Infineon·Microchip·GigaDevice 가
전부 CMSIS-Pack 으로 배포한다.

`.pack` 파일은 실은 ZIP 이라 그냥 풀어서 `.FLM` 을 꺼내면 된다.

```sh
unzip -j Keil.STM32F4xx_DFP.pack '*.FLM' -d <SD>/prog/loaders/flm/
```

pyOCD 를 이미 쓰고 있다면 받아둔 팩이 `~/.local/share/cmsis-pack-manager/` 나
`~/.pyocd/packs/` 에 있을 수 있다.
