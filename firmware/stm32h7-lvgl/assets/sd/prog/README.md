# /prog — 오프라인 다운로더

SD 카드의 이 폴더 하나에 다운로더가 쓰는 것이 전부 들어간다. 경로는 펌웨어의
`hw_def.h` 에서 `HW_SWD_SD_ROOT` 하나만 바꾸면 전부 따라온다.

```
/prog/
  mcu/       디바이스 DB. 이 폴더의 *.txt 를 전부 읽는다 (벤더별로 쪼갤 수 있다)
  loaders/   플래시 알고리즘. 같은 MCU 를 굽는 방법이 하나가 아니라 나눠 둔다
    flm/       CMSIS-Pack .FLM — 벤더 중립, 기본 경로
    st/        CubeProgrammer FlashLoader — ST 내부 플래시, 파일명이 DEV_ID
    ext/       CubeProgrammer ExternalLoader — 외부 QSPI / NOR / SDRAM
  fw/        프로젝트별 폴더. 각 폴더에 fw.txt 와 이미지가 들어간다
```

알고리즘 파일은 **확장자가 아니라 내용으로 판별**한다. `.FLM` 은 `DevDscr`,
`.stldr` 은 `StorageInfo` 를 보므로 어느 폴더에 두든 동작한다. 폴더를 나눈 건
사람이 고르기 좋으라는 것이지 코드가 그걸 보지는 않는다.
