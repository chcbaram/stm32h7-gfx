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

`.pack` 은 실은 ZIP 이라 풀어서 `.FLM` 을 꺼내도 되지만, 그러면 **어느 칩의
것인지와 알고리즘을 타깃 RAM 어디에 올려야 하는지가 빠진다.** RAM 주소는
`.FLM` 안에 없고 `.pdsc` 에만 있다. 그래서 스크립트를 쓴다.

```sh
# 팩 안에 뭐가 있는지
./tools/python/pack2db.py GigaDevice.GD32H7xx_DFP.1.4.0.pack --list
./tools/python/pack2db.py <pack> --list GD32H759        # 이름으로 거르기

# 모델을 지정하면 .FLM 추출 + DB 항목 생성까지
./tools/python/pack2db.py <pack> GD32H759IG
  [+] assets/sd/prog/loaders/flm/GD32H7xx_1MB.FLM
  [+] assets/sd/prog/mcu/gigadevice.txt   ([GD32H759IG] 추가)
```

자동 판별을 쓰려면 그 칩의 ID 레지스터를 알아야 한다. 알면 같이 넣는다.

```sh
./tools/python/pack2db.py <pack> GD32H759IG --id-addr 0xE0042000 --id-val 0x750
```

모르면 자동 판별만 안 될 뿐, `fw.txt` 에 `device = GD32H759IG` 라고 적으면
그대로 동작한다. **펌웨어는 고치지 않는다.**

pyOCD 를 이미 쓰고 있다면 받아둔 팩이 `~/.local/share/cmsis-pack-manager/` 나
`~/.pyocd/packs/` 에 있을 수 있다.

> 알고리즘 파일이 자기 크기를 틀리게 적은 경우가 실제로 있다. GigaDevice 팩의
> `GD32H7xx_1MB.FLM` 과 `2MB.FLM` 은 바이트까지 같은 파일이고 둘 다 자기를
> 3840KB 라고 소개한다. 그래서 DB 에 `.pdsc` 의 `flash_sz` 를 따로 적어 둔다 —
> 알고리즘 말만 믿으면 1MB 짜리 칩에서 범위 검사가 3840KB 까지 통과한다.
