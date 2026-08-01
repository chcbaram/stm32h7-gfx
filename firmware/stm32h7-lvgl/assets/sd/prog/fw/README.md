# /prog/fw — 구울 펌웨어

프로젝트마다 폴더를 하나 만든다.

```
/prog/fw/
  f411_lcd/
    app.bin      또는 app.elf / app.hex
    fw.txt       (예정)
```

이미지 형식은 **확장자가 아니라 내용으로 판별**한다.

| 형식 | 굽는 주소 |
|---|---|
| `.elf` | `PT_LOAD` 의 `p_paddr` — 파일이 안다 |
| `.hex` | 레코드가 안다 |
| `.bin` | **파일에 없다.** 명령에 주소를 적어야 한다 |

```
cli# prog write /prog/loaders/st/0x431.stldr /prog/fw/f411_lcd/app.elf 0x20001000
cli# prog write /prog/loaders/flm/STM32F4xx_512.FLM /prog/fw/f411_lcd/app.bin 0x20001000 0x08000000
```
