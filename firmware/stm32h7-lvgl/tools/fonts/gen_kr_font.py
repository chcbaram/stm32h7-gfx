#!/usr/bin/env python3
"""한글 LVGL .bin 폰트 생성 (KS X 1001 상용 2350자 + ASCII)

lv_font_conv(node) 로 압축 .bin 을 만든다. 결과는 SPI Flash 에 올려
lv_binfont_create("F:/font/kr.bin") 로 로드한다.

준비:
  # node 와 lv_font_conv 가 필요하다 (전역/로컬 어디든)
  npm install -g lv_font_conv        # 또는 npm install lv_font_conv

사용:
  ./gen_kr_font.py                                   # 기본값으로 kr.bin 생성
  ./gen_kr_font.py --size 24 -o kr_24.bin
  ./gen_kr_font.py --conv /path/to/lv_font_conv      # 실행 경로 직접 지정

업로드:
  ../python/upload.py -p PORT -d spi kr.bin /font/kr.bin
"""

import argparse
import os
import shutil
import subprocess
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TTF = os.path.join(HERE, "NanumGothicCoding-Bold.ttf")


def ksx1001_hangul():
    """KS X 1001 완성형 한글 2350자 코드포인트.

    EUC-KR 완성형 영역(고위 0xB0..0xC8, 하위 0xA1..0xFE)을 디코드해 뽑는다.
    (파이썬 'euc-kr' 코덱은 환경에 따라 cp949(11172자) 로 동작하므로
     인코딩이 아니라 바이트 디코드로 정확히 2350자만 얻는다.)
    """
    out = []
    for lead in range(0xB0, 0xC9):
        for trail in range(0xA1, 0xFF):
            try:
                ch = bytes([lead, trail]).decode("euc-kr")
            except Exception:
                continue
            if 0xAC00 <= ord(ch) <= 0xD7A3:
                out.append(ord(ch))
    return sorted(set(out))


def find_conv(explicit):
    if explicit:
        return explicit
    # PATH 에 있으면 그대로
    p = shutil.which("lv_font_conv")
    if p:
        return p
    # 흔한 로컬 설치 위치
    for cand in [
        os.path.join(HERE, "node_modules", ".bin", "lv_font_conv"),
        os.path.join(HERE, "..", "python", "node_modules", ".bin", "lv_font_conv"),
    ]:
        if os.path.exists(cand):
            return cand
    return None


def main():
    ap = argparse.ArgumentParser(description="한글 LVGL .bin 폰트 생성")
    ap.add_argument("-i", "--input", default=DEFAULT_TTF, help="TTF 경로")
    ap.add_argument("-o", "--output", default=os.path.join(HERE, "kr.bin"), help="출력 .bin")
    ap.add_argument("-s", "--size", type=int, default=28, help="픽셀 크기 (기본 28)")
    ap.add_argument("--bpp", type=int, default=4, help="비트/픽셀 (기본 4)")
    ap.add_argument("--no-compress", action="store_true", help="LVGL 압축 끄기 (파일/RAM 커짐)")
    ap.add_argument("--conv", help="lv_font_conv 실행 경로 (없으면 자동 탐색)")
    args = ap.parse_args()

    conv = find_conv(args.conv)
    if conv is None:
        print("lv_font_conv 를 찾지 못했습니다. `npm install -g lv_font_conv` 후 재시도하거나\n"
              "--conv 로 경로를 지정하세요.", file=sys.stderr)
        return 1

    chars = ksx1001_hangul()
    symbols = "".join(chr(c) for c in range(0x20, 0x7F)) + "".join(chr(c) for c in chars)
    print("KS X 1001 한글 %d 자 + ASCII" % len(chars))

    cmd = [conv, "--font", args.input, "--size", str(args.size),
           "--bpp", str(args.bpp), "--format", "bin",
           "--symbols", symbols, "-o", args.output]
    if args.no_compress:
        cmd.append("--no-compress")

    subprocess.run(cmd, check=True)
    print("생성 : %s (%d KB)" % (args.output, os.path.getsize(args.output) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
