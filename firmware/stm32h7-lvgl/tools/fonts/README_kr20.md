# kr_20.bin — 20px 한글 fallback

## 왜 필요한가

LVGL 의 fallback 폰트는 **크기를 바꾸지 않는다.** 그 폰트가 가진 크기 그대로
그린다. 그래서 28px 한글 하나로 20px 캡션까지 덮으면 한글만 40% 크게 나와
줄을 넘친다.

역할마다 같은 크기의 한글 폰트가 하나씩 있어야 한다.

    font_title   = montserrat_40  +  kr_28.bin(28)
    font_body    = montserrat_28  +  kr_28.bin(28)     크기가 맞는다
    font_caption = montserrat_20  +  kr_20.bin(20)  <- 이걸 위해 만들었다

## 만드는 법 — 공식 도구가 우선이다

**node 가 있으면 `gen_kr_font.py` 를 쓴다.** 그게 `lv_font_conv`(LVGL 공식
도구)를 부르고, 압축까지 해준다.

```sh
npm install -g lv_font_conv
./gen_kr_font.py --size 20 -o ../../assets/spi/font/kr_20.bin
```

node 가 없을 때만 `tools/python/font_bin.py` 를 쓴다. 이건 LVGL 의
`lv_binfont_loader.c` 를 거꾸로 구현한 것이라 공식 도구가 아니다.

```sh
../python/font_bin.py -i NanumGothicCoding-Bold.ttf -o ../../assets/spi/font/kr_20.bin -s 20 \
                      --textfile ksx1001.txt
```

무압축이라 공식 도구보다 파일이 조금 크다 (432KB 대 458KB — 28px 압축본과
비슷한 수준). SPI Flash 에 두므로 문제되지 않는다.

## 업로드

```sh
../python/upload.py -p PORT --sync ../../assets
```
