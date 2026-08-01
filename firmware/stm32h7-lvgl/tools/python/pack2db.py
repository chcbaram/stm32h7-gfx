#!/usr/bin/env python3
"""CMSIS-Pack 에서 MCU 를 찾아 .FLM 을 꺼내고 디바이스 DB 항목을 만든다.

  # 팩 안에 뭐가 있는지
  ./pack2db.py GigaDevice.GD32H7xx_DFP.1.4.0.pack --list
  ./pack2db.py <pack> --list GD32H759          # 이름으로 거르기

  # 모델을 지정하면 .FLM 추출 + DB 항목 생성까지
  ./pack2db.py <pack> GD32H759I
  ./pack2db.py <pack> GD32H759I --id-addr 0xE0042000 --id-val 0x750

  # 팩을 푼 폴더도 그대로 받는다
  ./pack2db.py ~/packs/GigaDevice/GD32H7xx_DFP/1.4.0 GD32H759I

unzip 과 뭐가 다른가
  `unzip -j '*.FLM'` 은 파일만 꺼낸다. 어느 칩의 것인지, 알고리즘을 타깃 RAM 의
  어디에 올려야 하는지가 빠진다. RAM 주소는 .FLM 안에 없고 .pdsc 에만 있어서,
  결국 손으로 찾아 적어야 한다. 이 스크립트는 그 두 가지를 같이 해준다.

왜 PC 에서 하나
  .pack 은 ZIP 이고 안의 .pdsc 는 XML 인데, family -> subFamily -> device 로
  속성이 상속된다. MCU 에서 하려면 inflate + XML 파서 + 상속 해석이 필요하고
  ST 의 DFP 는 수백 MB 라 스캔 자체가 느리다. 파이썬은 zipfile 과 xml.etree 가
  표준이라 같은 일을 훨씬 싸게 끝낸다.

  MCU 쪽 DB 형식은 그대로이므로, 나중에 보드에서 직접 .pack 을 읽고 싶어지면
  생성기만 추가하면 된다. 지금 결정이 그걸 막지 않는다.
"""

import argparse
import os
import posixpath
import re
import sys
import xml.etree.ElementTree as ET
import zipfile


DEFAULT_FLM_DIR = "assets/sd/prog/loaders/flm"
DEFAULT_MCU_DIR = "assets/sd/prog/mcu"
SD_FLM_DIR = "/prog/loaders/flm"


class Pack:
    """.pack(ZIP) 과 풀어놓은 폴더를 같은 얼굴로 다룬다."""

    def __init__(self, path):
        self.path = path
        self.zip = zipfile.ZipFile(path) if zipfile.is_zipfile(path) else None
        if self.zip is None and not os.path.isdir(path):
            raise ValueError(f"팩도 폴더도 아닙니다 : {path}")

    def names(self):
        if self.zip:
            return self.zip.namelist()
        out = []
        for root, _dirs, files in os.walk(self.path):
            for n in files:
                rel = os.path.relpath(os.path.join(root, n), self.path)
                out.append(rel.replace(os.sep, "/"))
        return out

    def read(self, name):
        if self.zip:
            return self.zip.read(name)
        with open(os.path.join(self.path, name.replace("/", os.sep)), "rb") as f:
            return f.read()

    def find(self, name):
        """.pdsc 가 가리키는 경로는 대소문자와 구분자가 어긋나는 일이 잦다."""
        want = name.replace("\\", "/").lstrip("./").lower()
        base = posixpath.basename(want)
        cands = self.names()

        for n in cands:
            if n.replace("\\", "/").lower() == want:
                return n
        for n in cands:                       # 파일명만 맞아도 받아준다
            if posixpath.basename(n.lower()) == base:
                return n
        return None

    def pdsc(self):
        for n in self.names():
            if n.lower().endswith(".pdsc") and "/" not in n.strip("/"):
                return n
        for n in self.names():                # 루트에 없으면 아무 데나
            if n.lower().endswith(".pdsc"):
                return n
        return None


def merge(base, node):
    """CMSIS-Pack 의 속성 상속. family 에 적힌 것이 device 까지 내려온다.

    memory 와 algorithm 은 쌓이되 같은 이름이면 더 구체적인 쪽이 이긴다.
    """
    out = dict(base)
    out["mem"] = dict(base.get("mem", {}))
    out["algo"] = dict(base.get("algo", {}))

    for p in node.findall("processor"):
        for k, v in p.attrib.items():
            out[k] = v

    for m in node.findall("memory"):
        key = m.get("id") or m.get("name")
        if key:
            out["mem"][key] = m.attrib

    for a in node.findall("algorithm"):
        key = posixpath.basename((a.get("name") or "").replace("\\", "/"))
        if key:
            out["algo"][key] = a.attrib

    return out


def walk_devices(pdsc_root):
    """family -> subFamily -> device 를 훑으며 상속을 해석한다."""
    devices = []

    for fam in pdsc_root.iter("family"):
        fam_ctx = merge({}, fam)
        fam_name = fam.get("Dfamily", "")

        def emit(dev, ctx, sub):
            name = dev.get("Dname") or dev.get("Dvariant")
            if not name:
                return
            devices.append(dict(name=name, family=fam_name, sub=sub,
                                ctx=merge(ctx, dev)))

        for dev in fam.findall("device"):
            emit(dev, fam_ctx, "")
        for sub in fam.findall("subFamily"):
            sub_ctx = merge(fam_ctx, sub)
            sub_name = sub.get("DsubFamily", "")
            for dev in sub.findall("device"):
                emit(dev, sub_ctx, sub_name)
            for var in sub.iter("variant"):
                emit(var, sub_ctx, sub_name)

    return devices


def pick_flash(ctx):
    """플래시 시작 주소와 크기. 알고리즘이 들고 있는 값과 교차 검증하는 용도다.

    GigaDevice 팩에서 1MB / 2MB / 3840KB 로 이름 붙은 .FLM 세 개가 전부 3840KB
    라고 자기를 소개하는 걸 봤다 (1MB 와 2MB 는 바이트까지 같은 파일이다).
    알고리즘 말만 믿으면 1MB 짜리 칩에서 범위 검사가 3840KB 까지 통과한다.
    """
    best = None
    for key, a in ctx.get("mem", {}).items():
        if not key.upper().startswith("IROM"):
            continue
        try:
            start = int(a["start"], 0)
            size = int(a["size"], 0)
        except (KeyError, ValueError):
            continue
        score = (1 if a.get("default") in ("1", "true") else 0, size)
        if best is None or score > best[0]:
            best = (score, start, size)
    return (best[1], best[2]) if best else (None, None)


def pick_ram(ctx):
    """알고리즘을 올릴 RAM. default=1 인 IRAM 을 우선하고 없으면 가장 큰 것."""
    best = None
    for key, a in ctx.get("mem", {}).items():
        acc = (a.get("access") or "").lower()
        is_ram = key.upper().startswith("IRAM") or "w" in acc
        if not is_ram:
            continue
        try:
            start = int(a["start"], 0)
            size = int(a["size"], 0)
        except (KeyError, ValueError):
            continue
        score = (1 if a.get("default") in ("1", "true") else 0, size)
        if best is None or score > best[0]:
            best = (score, start, size)
    return (best[1], best[2]) if best else (None, None)


def pick_algo(ctx):
    """기본 플래시 알고리즘. default=1 을 우선하고 없으면 첫 번째."""
    algos = ctx.get("algo", {})
    if not algos:
        return None
    for name, a in algos.items():
        if a.get("default") in ("1", "true"):
            return a.get("name")
    return next(iter(algos.values())).get("name")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack", help=".pack 파일 또는 풀어놓은 폴더")
    ap.add_argument("device", nargs="?", help="MCU 모델. 일부만 적어도 된다")
    ap.add_argument("--list", nargs="?", const="", metavar="필터",
                    help="디바이스 목록만 보여준다")
    ap.add_argument("--flm-dir", default=DEFAULT_FLM_DIR)
    ap.add_argument("--mcu-dir", default=DEFAULT_MCU_DIR)
    ap.add_argument("--out", help="DB 파일 (기본 <mcu-dir>/<벤더>.txt)")
    ap.add_argument("--id-addr", help="디바이스 ID 레지스터 주소. 알면 적어라 (자동 판별용)")
    ap.add_argument("--id-mask", default="0xFFF")
    ap.add_argument("--id-val", help="그 주소에서 읽힐 값")
    args = ap.parse_args()

    try:
        pack = Pack(args.pack)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1

    pdsc_name = pack.pdsc()
    if pdsc_name is None:
        print("팩 안에 .pdsc 가 없습니다", file=sys.stderr)
        return 1

    root = ET.fromstring(pack.read(pdsc_name))
    vendor = (root.findtext("vendor") or "vendor").split(":")[0].strip()
    devices = walk_devices(root)

    if not devices:
        print(".pdsc 에서 디바이스를 찾지 못했습니다", file=sys.stderr)
        return 1

    # ---- 목록만
    if args.list is not None or args.device is None:
        flt = (args.list or "").lower()
        n = 0
        print(f"{os.path.basename(args.pack)}  vendor={vendor}  {len(devices)} 개")
        for d in sorted(devices, key=lambda x: x["name"]):
            if flt and flt not in d["name"].lower():
                continue
            ram, ram_sz = pick_ram(d["ctx"])
            algo = pick_algo(d["ctx"])
            print(f"  {d['name']:<22} {d['ctx'].get('Dcore',''):<12} "
                  f"ram {('0x%08X' % ram) if ram else '?':<10} "
                  f"{(ram_sz // 1024) if ram_sz else 0:>5} KB  "
                  f"{posixpath.basename(algo) if algo else '(알고리즘 없음)'}")
            n += 1
        if flt:
            print(f"  -> {n} 개")
        if args.device is None and args.list is None:
            print("\n모델을 지정하면 .FLM 추출과 DB 항목 생성까지 합니다.")
        return 0

    # ---- 모델 지정
    want = args.device.lower()
    hits = [d for d in devices if d["name"].lower().startswith(want)]
    if not hits:
        hits = [d for d in devices if want in d["name"].lower()]
    if not hits:
        print(f"'{args.device}' 에 맞는 디바이스가 없습니다. --list 로 확인하세요.",
              file=sys.stderr)
        return 1
    if len(hits) > 1:
        exact = [d for d in hits if d["name"].lower() == want]
        if exact:
            hits = exact
        else:
            print(f"'{args.device}' 가 {len(hits)} 개와 맞습니다. 더 구체적으로 적으세요:",
                  file=sys.stderr)
            for d in hits[:20]:
                print(f"  {d['name']}", file=sys.stderr)
            return 1

    dev = hits[0]
    ctx = dev["ctx"]
    ram, ram_sz = pick_ram(ctx)
    flash, flash_sz = pick_flash(ctx)
    algo = pick_algo(ctx)

    if algo is None:
        print(f"{dev['name']} 에 플래시 알고리즘이 없습니다", file=sys.stderr)
        return 1

    src = pack.find(algo)
    if src is None:
        print(f"팩 안에서 {algo} 를 찾지 못했습니다", file=sys.stderr)
        return 1

    flm_name = posixpath.basename(algo.replace("\\", "/"))
    os.makedirs(args.flm_dir, exist_ok=True)
    flm_out = os.path.join(args.flm_dir, flm_name)
    with open(flm_out, "wb") as f:
        f.write(pack.read(src))

    # ---- DB 항목
    out_path = args.out or os.path.join(args.mcu_dir,
                                        re.sub(r"[^A-Za-z0-9]+", "", vendor).lower() + ".txt")
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    lines = []
    if os.path.exists(out_path):
        with open(out_path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    else:
        lines = [
            f"# {vendor} 디바이스 DB",
            "#",
            "#   tools/python/pack2db.py 가 CMSIS-Pack 에서 만든다.",
            "#   id_addr 이 없는 항목은 자동 판별이 안 된다 — 이름을 직접 지정해야 한다.",
            "#   ID 레지스터 주소를 알면 --id-addr / --id-val 로 넣어라.",
            "",
        ]

    # 같은 이름의 기존 항목은 통째로 걷어낸다
    out, skip = [], False
    for ln in lines:
        if ln.startswith("["):
            skip = (ln.strip() == f"[{dev['name']}]")
        if not skip:
            out.append(ln)
    while out and out[-1].strip() == "":
        out.pop()

    entry = [f"[{dev['name']}]"]
    if ctx.get("Dcore"):
        entry.append(f"cpu     = {ctx['Dcore']}")
    if args.id_addr and args.id_val:
        entry.append(f"id_addr = 0x{int(args.id_addr, 0):08X}")
        entry.append(f"id_mask = 0x{int(args.id_mask, 0):08X}")
        entry.append(f"id_val  = 0x{int(args.id_val, 0):08X}")
    if ram is not None:
        entry.append(f"ram     = 0x{ram:08X}")
        entry.append(f"ram_sz  = 0x{ram_sz:X}")
    if flash is not None:
        entry.append(f"flash   = 0x{flash:08X}")
        entry.append(f"flash_sz = 0x{flash_sz:X}")
    entry.append(f"algo    = {SD_FLM_DIR}/{flm_name}")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out + [""] + entry + [""]) + "\n")

    print(f"[+] {flm_out}   ({len(pack.read(src))} bytes)")
    print(f"[+] {out_path}   ([{dev['name']}] 추가)")
    print()
    print(f"  이름   : {dev['name']}  ({ctx.get('Dcore','?')})")
    print(f"  ram    : " + (f"0x{ram:08X}  {ram_sz // 1024} KB" if ram else "?"))
    if flash is not None:
        print(f"  flash  : 0x{flash:08X}  {flash_sz // 1024} KB")
    print(f"  algo   : {SD_FLM_DIR}/{flm_name}")
    if not (args.id_addr and args.id_val):
        print()
        print("  자동 판별은 안 된다 (ID 레지스터를 모른다).")
        print(f"  fw.txt 에 device = {dev['name']} 을 적거나,")
        print("  주소를 알면 --id-addr / --id-val 로 다시 돌려라.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
