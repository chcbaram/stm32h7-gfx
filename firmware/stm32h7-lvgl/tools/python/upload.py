#!/usr/bin/env python3
"""USB CDC 로 보드에 파일을 올린다.

  # 포트 목록
  ./upload.py --list

  # 스테이징 폴더를 통째로 (권장)
  #   assets/sd/**  -> SD 카드
  #   assets/spi/** -> SPI Flash (littlefs)
  ./upload.py -p PORT --sync assets

  # 파일 하나
  ./upload.py -p PORT logo.bin /ui/logo.bin
  ./upload.py -p PORT -d spi cfg.bin /cfg.bin

  # 폴더 하나 (하위 폴더까지)
  ./upload.py -p PORT --dir assets/ui /ui
"""

import argparse
import os
import sys
import time

from cmd_link import CmdLink, CmdError, crc16, BAUD_CMD

FILE_CMD_INFO = 0x0100
FILE_CMD_BEGIN = 0x0101
FILE_CMD_WRITE = 0x0102
FILE_CMD_END = 0x0103
FILE_CMD_DEL = 0x0104
FILE_CMD_MKDIR = 0x0105

DRIVE_SD = 0
DRIVE_SPI = 1

# 'fs' 는 펌웨어 enum(FILE_DRIVE_FS) 이름과 맞춘 별칭
DRIVE = {"sd": DRIVE_SD, "spi": DRIVE_SPI, "fs": DRIVE_SPI}
DRIVE_NAME = {DRIVE_SD: "SD", DRIVE_SPI: "SPI Flash"}

# --sync 가 인식하는 스테이징 폴더 이름
SYNC_DIRS = {"sd": DRIVE_SD, "spi": DRIVE_SPI, "fs": DRIVE_SPI}


def list_ports():
    from serial.tools import list_ports as lp

    ports = list(lp.comports())
    if not ports:
        print("직렬 포트를 찾지 못했습니다.")
        return
    for p in ports:
        print(f"  {p.device:32s} {p.description}")


def get_info(link):
    data = link.request(FILE_CMD_INFO)
    max_data = data[0] | (data[1] << 8)
    sd_ready = data[2]
    fs_free = int.from_bytes(data[3:7], "little", signed=True)
    return max_data, sd_ready, fs_free


def collect(base, dest_root):
    """base 아래 모든 파일을 (로컬경로, 보드경로) 로 모은다. 하위 폴더 포함."""
    base = base.rstrip("/")
    dest_root = "/" + dest_root.strip("/") if dest_root.strip("/") else ""
    out = []

    for root, dirs, names in os.walk(base):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for n in sorted(names):
            if n.startswith("."):
                continue
            local = os.path.join(root, n)
            rel = os.path.relpath(local, base).replace(os.sep, "/")
            out.append((local, f"{dest_root}/{rel}"))
    return out


def parent_dirs(paths):
    """만들어야 할 디렉토리를 상위부터 순서대로 (중간 단계 포함) 돌려준다."""
    needed = set()
    for p in paths:
        parts = p.strip("/").split("/")[:-1]
        for i in range(1, len(parts) + 1):
            needed.add("/" + "/".join(parts[:i]))
    return sorted(needed, key=lambda s: (s.count("/"), s))


def make_dirs(link, drive, paths, dry_run=False):
    for d in parent_dirs(paths):
        if dry_run:
            print(f"  mkdir  {DRIVE_NAME[drive]:9s} {d}")
            continue
        try:
            link.request(FILE_CMD_MKDIR, bytes([drive]) + d.encode())
        except CmdError:
            pass  # 이미 존재


def upload_file(link, drive, local_path, remote_path, chunk_size):
    with open(local_path, "rb") as f:
        payload = f.read()

    size = len(payload)
    header = bytes([drive]) + size.to_bytes(4, "little") + remote_path.encode()
    link.request(FILE_CMD_BEGIN, header)

    show_progress = sys.stdout.isatty() and size > 64 * 1024

    t0 = time.time()
    offset = 0
    pct_pre = -1
    while offset < size:
        piece = payload[offset:offset + chunk_size]
        link.request(FILE_CMD_WRITE, offset.to_bytes(4, "little") + piece)
        offset += len(piece)

        if show_progress:
            pct = offset * 100 // size
            if pct != pct_pre:
                pct_pre = pct
                print(f"\r  {remote_path:44s} {pct:3d}%", end="", flush=True)

    link.request(FILE_CMD_END, crc16(payload).to_bytes(2, "little"))

    elapsed = time.time() - t0
    rate = (size / 1024 / elapsed) if elapsed > 0 else 0
    print(f"\r  {remote_path:44s} OK  {size:>8} B  {elapsed:5.1f}s  {rate:6.1f} KB/s")


def upload_set(link, drive, files, chunk, dry_run):
    if not files:
        return
    print(f"[{DRIVE_NAME[drive]}] {len(files)} 개")
    make_dirs(link, drive, [r for _, r in files], dry_run)
    for local, remote in files:
        if dry_run:
            print(f"  put    {os.path.getsize(local):>8} B  {remote}")
        else:
            upload_file(link, drive, local, remote, chunk)


def main():
    ap = argparse.ArgumentParser(description="stm32h7-lvgl 에셋 업로더")
    ap.add_argument("src", nargs="?", help="로컬 파일 또는 폴더")
    ap.add_argument("dest", nargs="?", help="보드상의 경로")
    ap.add_argument("-p", "--port", help="직렬 포트")
    ap.add_argument("-b", "--baud", type=int, default=BAUD_CMD,
                    help=f"보율 (기본 {BAUD_CMD}). 115200 은 CLI 용이라 쓰면 안 된다")
    ap.add_argument("-d", "--drive", choices=["sd", "spi", "fs"], default="sd",
                    help="sd = SD 카드, spi = SPI Flash (기본 sd)")
    ap.add_argument("--sync", metavar="DIR",
                    help="스테이징 폴더 통째로. 안의 sd/ 와 spi/ 가 대상을 결정한다")
    ap.add_argument("--dir", action="store_true", help="src 를 폴더로 보고 하위까지 전송")
    ap.add_argument("--chunk", type=int, default=0, help="청크 크기 (기본: 보드가 알려준 최대치)")
    ap.add_argument("--dry-run", action="store_true", help="전송 없이 계획만 출력")
    ap.add_argument("--list", action="store_true", help="직렬 포트 목록")
    args = ap.parse_args()

    if args.list:
        list_ports()
        return 0

    # --- 무엇을 보낼지 먼저 정한다 (연결 전에 확인 가능) ---
    plan = {DRIVE_SD: [], DRIVE_SPI: []}

    if args.sync:
        root = args.sync.rstrip("/")
        if not os.path.isdir(root):
            print(f"폴더가 없습니다 : {root}", file=sys.stderr)
            return 1

        found = False
        for name in sorted(os.listdir(root)):
            sub = os.path.join(root, name)
            if not os.path.isdir(sub):
                continue
            if name.lower() not in SYNC_DIRS:
                print(f"건너뜀 : {name}/ (sd/ 또는 spi/ 가 아님)")
                continue
            found = True
            plan[SYNC_DIRS[name.lower()]] += collect(sub, "")

        if not found:
            print(f"{root}/ 안에 sd/ 또는 spi/ 폴더가 없습니다.", file=sys.stderr)
            return 1
    else:
        if not args.src or not args.dest:
            ap.error("src 와 dest 가 필요합니다 (또는 --sync 사용)")
        drive = DRIVE[args.drive]
        if args.dir:
            plan[drive] = collect(args.src, args.dest)
        else:
            plan[drive] = [(args.src, "/" + args.dest.strip("/"))]

    total_files = sum(len(v) for v in plan.values())
    total_bytes = sum(os.path.getsize(l) for v in plan.values() for l, _ in v)
    if total_files == 0:
        print("올릴 파일이 없습니다.")
        return 0

    print(f"대상 {total_files} 개 파일, {total_bytes:,} bytes")

    if args.dry_run:
        for drive, files in plan.items():
            upload_set(None, drive, files, 0, True)
        return 0

    # --- 실제 전송 ---
    if not args.port:
        ap.error("-p/--port 가 필요합니다 (--list 로 확인)")
    if args.baud == 115200:
        print("경고: 115200 은 펌웨어가 CLI 로 인식합니다. 다른 보율을 쓰세요.", file=sys.stderr)
        return 1

    with CmdLink(args.port, args.baud) as link:
        max_data, sd_ready, fs_free = get_info(link)
        print(f"연결됨  max_data={max_data}  SD={'있음' if sd_ready else '없음'}  "
              f"SPI Flash 여유={fs_free:,} bytes")

        if plan[DRIVE_SD] and not sd_ready:
            print("SD 카드가 감지되지 않았습니다.", file=sys.stderr)
            return 1

        # WRITE 패킷 앞 4바이트가 offset 이라 그만큼 뺀다
        chunk = args.chunk if args.chunk else (max_data - 4)

        t0 = time.time()
        for drive, files in plan.items():
            upload_set(link, drive, files, chunk, False)

        print(f"완료  {total_files} 개, {total_bytes:,} bytes, {time.time()-t0:.1f}s")

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
    except KeyboardInterrupt:
        print("\n중단됨", file=sys.stderr)
        sys.exit(130)
