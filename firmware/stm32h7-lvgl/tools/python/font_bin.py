#!/usr/bin/env python3
"""TTF -> LVGL v9 binfont(.bin). node 없이 만든다.

  ./font_bin.py -i ../fonts/NanumGothicCoding-Bold.ttf -o kr_20.bin -s 20
  ./font_bin.py -i font.ttf -o kr_20.bin -s 20 --textfile chars.txt

왜 필요한가
  한글 fallback 폰트가 28px 하나뿐이라, 20px 캡션 라벨에 한글이 섞이면 글자가
  40% 크게 나와 줄을 넘친다. 역할마다 같은 크기의 한글 폰트를 주려면 크기별로
  하나씩 있어야 한다.

  공식 도구인 lv_font_conv 는 node 가 필요하다. 그게 없어도 폰트를 다시 만들 수
  있어야 해서 여기에 둔다. tools/python/font_conv.py 는 C 소스를 뱉는데, 그건
  플래시를 먹는다. .bin 은 SPI Flash 나 SD 에 두므로 크기 걱정이 없다.

  포맷은 src/lib/lvgl/src/font/binfont_loader/lv_binfont_loader.c 를 거꾸로
  구현한 것이다. 로더가 읽는 방식대로만 쓰면 된다.
    - cmap 은 SPARSE_FULL(3) 하나로 낸다. 한글은 어차피 흩어져 있어서 범위
      압축이 의미가 없고, 구현이 가장 단순하다.
    - glyf 는 무압축(compression_id = 0), 4bpp.
    - kern 은 넣지 않는다 (tables_count 에서 뺀다).
"""

import argparse
import struct
import sys

import freetype


ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

CMAP_SPARSE_FULL = 3


def collect_chars(args):
    chars = set(ASCII)
    if args.text:
        chars |= set(args.text)
    if args.textfile:
        with open(args.textfile, encoding="utf-8") as f:
            chars |= set(f.read())
    for ch in ("\n", "\r", "\t"):
        chars.discard(ch)
    return sorted(chars, key=ord)


class Glyph:
    __slots__ = ("cp", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y", "bits")


def render(face, cp):
    """4bpp 연속 비트스트림. LVGL 이 읽는 순서(MSB first)와 같게 담는다."""
    face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
    g = face.glyph
    bmp = g.bitmap
    w, h, pitch = bmp.width, bmp.rows, bmp.pitch
    buf = bmp.buffer

    gl = Glyph()
    gl.cp = ord(cp)
    gl.adv_w = round(g.advance.x / 64.0 * 16)      # 4비트 소수부
    gl.box_w = w
    gl.box_h = h
    gl.ofs_x = g.bitmap_left
    gl.ofs_y = g.bitmap_top - h

    data = bytearray()
    acc = 0
    nbits = 0
    for r in range(h):
        base = r * pitch
        for c in range(w):
            acc = (acc << 4) | (buf[base + c] >> 4)
            nbits += 4
            if nbits == 8:
                data.append(acc & 0xFF)
                acc = 0
                nbits = 0
    if nbits:
        data.append((acc << (8 - nbits)) & 0xFF)

    gl.bits = bytes(data)
    return gl


class BitWriter:
    """glyf 는 필드가 비트 단위로 붙어 있다. 글리프마다 바이트 경계로 맞춘다."""

    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def write(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc & 0xFF)
                self.acc = 0
                self.n = 0

    def write_signed(self, value, bits):
        self.write(value & ((1 << bits) - 1), bits)

    def write_bytes(self, data):
        for b in data:
            self.write(b, 8)

    def align(self):
        if self.n:
            self.acc <<= (8 - self.n)
            self.out.append(self.acc & 0xFF)
            self.acc = 0
            self.n = 0
        return len(self.out)


def bits_for(value):
    n = 1
    while (1 << n) <= value:
        n += 1
    return n


def label(name, payload):
    """테이블 = 길이(4B, 자기 포함) + 라벨 4B + 내용"""
    return struct.pack("<I", 8 + len(payload)) + name.encode() + payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="TTF 경로")
    ap.add_argument("-o", "--output", required=True, help="출력 .bin")
    ap.add_argument("-s", "--size", type=int, default=20, help="픽셀 크기")
    ap.add_argument("--text", help="포함할 문자열")
    ap.add_argument("--textfile", help="포함할 문자가 든 파일")
    args = ap.parse_args()

    chars = collect_chars(args)

    face = freetype.Face(args.input)
    face.set_pixel_sizes(0, args.size)

    glyphs = []
    for ch in chars:
        try:
            glyphs.append(render(face, ch))
        except Exception as e:                     # 글리프가 없는 문자는 건너뛴다
            print(f"건너뜀 U+{ord(ch):04X} : {e}", file=sys.stderr)

    if not glyphs:
        print("글리프가 하나도 없습니다", file=sys.stderr)
        return 1

    ascent = face.size.ascender >> 6
    descent = face.size.descender >> 6             # 음수
    max_w = max(g.box_w for g in glyphs)
    max_h = max(g.box_h for g in glyphs)
    max_adv = max(g.adv_w for g in glyphs)
    min_xy = min(min(g.ofs_x for g in glyphs), min(g.ofs_y for g in glyphs))
    max_xy = max(max(g.ofs_x for g in glyphs), max(g.ofs_y for g in glyphs))

    wh_bits = max(bits_for(max(max_w, max_h)), 1)
    adv_bits = max(bits_for(max_adv), 1)
    xy_bits = max(bits_for(max(abs(min_xy), abs(max_xy))) + 1, 2)   # 부호 포함
    #
    # 헤더 비트수를 8 의 배수로 맞춘다.
    #
    # 로더는 bmp_size = (다음 오프셋 - 이 오프셋) - nbits/8 로 비트맵 바이트
    # 수를 구한다. nbits 가 8 의 배수가 아니면 이 정수 나눗셈에서 한 바이트가
    # 어긋나고, 마지막 바이트를 비트 단위로 긁어모으는 느린 경로로 간다.
    # 8 의 배수면 bmp_size 가 정확히 맞고 lv_fs_read 로 통째로 읽는다.
    # 남는 비트는 adv 에 준다 - 어차피 값 범위만 넓어질 뿐이다. 
    while (adv_bits + 2 * xy_bits + 2 * wh_bits) % 8:
        adv_bits += 1

    # ---- glyf : 인덱스 0 은 빈 글리프여야 한다 (로더가 그렇게 읽는다)
    bw = BitWriter()
    # 오프셋은 glyf 테이블의 시작(길이 필드) 기준이다. 로더가
    # lv_fs_seek(start + glyph_offset[i]) 로 읽으므로 라벨 8바이트를 포함한다.
    GLYF_HDR = 8
    offsets = [GLYF_HDR]
    bw.write(0, adv_bits)
    bw.write_signed(0, xy_bits)
    bw.write_signed(0, xy_bits)
    bw.write(0, wh_bits)
    bw.write(0, wh_bits)
    offsets.append(GLYF_HDR + bw.align())

    for g in glyphs:
        bw.write(g.adv_w, adv_bits)
        bw.write_signed(g.ofs_x, xy_bits)
        bw.write_signed(g.ofs_y, xy_bits)
        bw.write(g.box_w, wh_bits)
        bw.write(g.box_h, wh_bits)
        bw.write_bytes(g.bits)
        offsets.append(GLYF_HDR + bw.align())

    glyf_data = bytes(bw.out)
    glyf = label("glyf", glyf_data)

    # ---- loca : 글리프마다 glyf 안의 오프셋
    loca_payload = struct.pack("<I", len(offsets) - 1)
    for o in offsets[:-1]:
        loca_payload += struct.pack("<I", o)
    loca = label("loca", loca_payload)

    # ---- cmap : SPARSE_FULL 하나. 유니코드 목록 + 글리프ID 오프셋 목록
    cps = [g.cp for g in glyphs]
    rng_start = cps[0]

    # SPARSE_FULL 은 (범위 시작 기준 오프셋 목록) + (글리프 ID 오프셋 목록)
    unicode_list = b"".join(struct.pack("<H", cp - rng_start) for cp in cps)
    gid_list = b"".join(struct.pack("<H", i + 1) for i in range(len(cps)))
    cmap_sub = unicode_list + gid_list

    # data_offset 은 cmaps_start(길이 필드 시작) 기준이다.
    #   4(길이) + 4(라벨) + 4(서브테이블 개수) + 16(서브테이블 1개)
    data_offset = 4 + 4 + 4 + 16
    cmap_tbl = struct.pack("<IIHHHBB",
                           data_offset,
                           rng_start,
                           cps[-1] - rng_start + 1,   # range_length
                           1,                         # glyph_id_start
                           len(cps),                  # data_entries_count
                           CMAP_SPARSE_FULL,
                           0)
    cmap = label("cmap", struct.pack("<I", 1) + cmap_tbl + cmap_sub)

    # ---- head
    head_payload = struct.pack("<IHHHhHhHhhHHBBBBBBBBBBhH",
                               1,                   # version
                               3,   # tables_count : kern 없음
                               args.size,
                               ascent, descent,
                               ascent, descent, 0,  # typo_*
                               min_xy, max_h,       # min_y, max_y
                               max_adv,             # default_advance_width
                               0,                   # kerning_scale
                               1,                   # index_to_loc_format (32bit)
                               0,                   # glyph_id_format
                               1,                   # advance_width_format (4비트 소수 포함)
                               4,                   # bits_per_pixel
                               xy_bits, wh_bits, adv_bits,
                               0,                   # compression_id (무압축)
                               0,                   # subpixels_mode
                               0,                   # padding
                               0, 0)                # underline position/thickness
    head = label("head", head_payload)

    with open(args.output, "wb") as f:
        f.write(head + cmap + loca + glyf)

    total = len(head) + len(cmap) + len(loca) + len(glyf)
    print(f"{args.output}")
    print(f"  글리프 : {len(glyphs)} 자  ({args.size}px, 4bpp)")
    print(f"  비트   : xy {xy_bits}  wh {wh_bits}  adv {adv_bits}")
    print(f"  크기   : {total:,} bytes  (head {len(head)} / cmap {len(cmap)} / "
          f"loca {len(loca)} / glyf {len(glyf)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
