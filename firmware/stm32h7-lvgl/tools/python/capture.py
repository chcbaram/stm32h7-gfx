#!/usr/bin/env python3
"""보드 화면을 캡쳐해서 PNG 로 저장한다.

  ./capture.py -p PORT screen.png
  ./capture.py -p PORT --raw screen.png     # 압축 없이 (디버깅용)

표시중인 LTDC 프레임버퍼를 보드에서 한 번 떠놓고 읽어오므로
전송 도중 화면이 갱신되어도 찢어지지 않는다.
"""

import argparse
import struct
import sys
import time
import zlib

from cmd_link import CmdLink, CmdError, BAUD_CMD

SCREEN_CMD_INFO = 0x0200
SCREEN_CMD_CAPTURE = 0x0201
SCREEN_CMD_READ = 0x0202
SCREEN_CMD_END = 0x0203

FMT_RGB565 = 0
FMT_RLE565 = 1


def rgb565_to_rgb888(value):
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    # 상위 비트를 하위로 복제해야 흰색이 255 가 된다
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def decode(payload, fmt, width, height):
    """전송받은 데이터를 RGB888 바이트열로 푼다."""
    out = bytearray(width * height * 3)
    pos = 0

    if fmt == FMT_RGB565:
        for i in range(0, len(payload), 2):
            r, g, b = rgb565_to_rgb888(payload[i] | (payload[i + 1] << 8))
            out[pos] = r; out[pos + 1] = g; out[pos + 2] = b
            pos += 3
    else:
        for i in range(0, len(payload), 3):
            run = payload[i]
            r, g, b = rgb565_to_rgb888(payload[i + 1] | (payload[i + 2] << 8))
            for _ in range(run):
                if pos + 3 > len(out):
                    break
                out[pos] = r; out[pos + 1] = g; out[pos + 2] = b
                pos += 3

    if pos != len(out):
        print(f"경고: 픽셀 수 불일치  {pos//3} / {width*height}", file=sys.stderr)
    return bytes(out)


def write_png(path, rgb, width, height):
    """Pillow 없이 PNG 를 쓴다. zlib 은 표준 라이브러리."""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    # 스캔라인마다 필터 바이트 0 을 앞에 붙인다
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description="보드 화면 캡쳐")
    ap.add_argument("out", nargs="?", default="screen.png", help="저장할 PNG 경로")
    ap.add_argument("-p", "--port", required=True, help="직렬 포트")
    ap.add_argument("-b", "--baud", type=int, default=BAUD_CMD)
    ap.add_argument("--raw", action="store_true", help="RLE 압축 없이 받는다")
    args = ap.parse_args()

    with CmdLink(args.port, args.baud, timeout=3.0) as link:
        data = link.request(SCREEN_CMD_INFO)
        width, height, bpp, max_fmt = struct.unpack("<HHBB", data[:6])
        print(f"화면 {width}x{height} {bpp}bpp")

        want = FMT_RGB565 if args.raw else min(FMT_RLE565, max_fmt)

        t0 = time.time()
        data = link.request(SCREEN_CMD_CAPTURE, bytes([want]))
        fmt = data[0]
        total = int.from_bytes(data[1:5], "little")

        raw_len = width * height * 2
        ratio = raw_len / total if total else 1
        print(f"캡쳐 {'RLE' if fmt == FMT_RLE565 else 'RAW'}  "
              f"{total:,} bytes  (원본 {raw_len:,}, {ratio:.1f}배 압축)")

        payload = bytearray()
        chunk_size = 1024
        while len(payload) < total:
            req = min(chunk_size, total - len(payload))
            part = link.request(SCREEN_CMD_READ,
                                len(payload).to_bytes(4, "little") + req.to_bytes(2, "little"))
            if not part:
                raise ValueError(f"빈 응답  offset={len(payload)}")
            payload += part

            pct = len(payload) * 100 // total
            if sys.stdout.isatty():
                print(f"\r  수신 {pct:3d}%", end="", flush=True)

        elapsed = time.time() - t0
        try:
            link.request(SCREEN_CMD_END)
        except CmdError:
            pass

        print(f"\r  수신 {len(payload):,} bytes, {elapsed:.1f}s "
              f"({len(payload)/1024/elapsed:.0f} KB/s)")

    rgb = decode(bytes(payload), fmt, width, height)
    write_png(args.out, rgb, width, height)
    print(f"저장 : {args.out}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CmdError as e:
        print(f"\n실패: {e}", file=sys.stderr)
        sys.exit(1)
    except (TimeoutError, ValueError) as e:
        print(f"\n통신 오류: {e}", file=sys.stderr)
        sys.exit(1)
