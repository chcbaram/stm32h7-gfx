# 한글 폰트

UI 의 한글(파일명 등 빌드 시점에 알 수 없는 문자)을 렌더하기 위한 폰트다.

- `kr.bin` — 나눔고딕(KS X 1001 상용 2350자 + ASCII) 28px 4bpp, LVGL 압축.
  SPI Flash 에 올려 `lv_binfont_create("F:/font/kr.bin")` 로 로드한다.
  영문 테마 폰트(montserrat)의 `fallback` 으로 연결되어, 영문은 montserrat,
  한글은 이 폰트로 자동 표시된다. (`src/ap/modules/ui/ui.c`, `ui_theme.c`)
- `NanumGothicCoding-Bold.ttf` — 원본 TTF (재생성용).
- `gen_kr_font.py` — `kr.bin` 재생성 스크립트.

## 보드에 올리기

```
../python/upload.py -p <PORT> -d spi kr.bin /font/kr.bin
```

폰트가 없으면 UI 는 영문만 표시된다(한글은 빈칸). 즉 이 파일은 펌웨어와
별개로 SPI Flash 에 상주한다.

## 재생성

`lv_font_conv`(node) 가 필요하다.

```
npm install -g lv_font_conv
./gen_kr_font.py                 # kr.bin 생성
```

전체 완성형 11172자가 필요하면 `gen_kr_font.py` 의 문자 범위를
`0xAC00-0xD7A3` 전체로 바꾸면 되지만, 파일이 ~4배 커지고 LVGL 힙도
그만큼 더 필요하다. 상용 2350자로 일상 한국어는 사실상 모두 커버된다.

## 참고

- 폰트는 로드 시 압축 상태로 RAM(LVGL 힙)에 올라간다. `LV_USE_FONT_COMPRESSED=1`
  이면 글자 그릴 때 해제한다. `LV_MEM_SIZE` 는 이를 감안해 2MB 로 잡혀 있다.
- 크기를 키우려면 `--size` 로 다른 크기의 bin 을 만들어 별도 파일로 올리고
  역할별로 로드하면 된다.
