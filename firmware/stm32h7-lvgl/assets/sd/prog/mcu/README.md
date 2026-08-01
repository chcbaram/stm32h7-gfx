# /prog/mcu — 디바이스 DB

이 폴더의 `*.txt` 를 전부 읽는다. 벤더별로 파일을 나눠도 되고 한 파일에 몰아도
된다.

## st.txt (저장소에 들어 있다)

STM32CubeProgrammer 의 `Data_Base` 에서 만든 것이다. 104개 항목, 18 KB.

```sh
# CubeProgrammer 버전이 올라가면 다시 만든다
./tools/python/cubedb2txt.py /opt/ST/STM32CubeCLT_1.22.0/STM32CubeProgrammer
```

## 형식

```ini
[STM32F411xC/E]
cpu     = Cortex-M4
id_addr = 0xE0042000        # 이 주소를 읽어서
id_mask = 0x00000FFF        # 이 비트가
id_val  = 0x00000431        # 이 값이면 이 디바이스
ram     = 0x20000000        # 알고리즘을 올릴 자리
ram_sz  = 0x10000
algo    = /prog/loaders/st/0x431.stldr
```

`id_addr / id_mask / id_val` 3연이 **벤더 중립의 핵심**이다. "어느 주소를 읽어
어느 비트가 무엇이면 이 디바이스" 라고만 적으므로, ST 의 DBGMCU 든 Nordic 의
FICR 든 Microchip 의 DSU 든 코드 변경 없이 항목만 추가하면 된다.

`ram / ram_sz` 는 `.FLM` 에도 `.stldr` 에도 없는 값이라 이 DB 를 쓰는 가장 큰
이유다. 이게 없으면 알고리즘을 어디에 올릴지 사람이 매번 지정해야 한다.

## 다른 제조사를 추가하려면

같은 형식으로 `nordic.txt` 같은 파일을 만들어 넣으면 된다. 맞는 `.FLM` 을
`/prog/loaders/flm/` 에 넣고 `algo` 로 가리킨다. **펌웨어는 고치지 않는다.**
