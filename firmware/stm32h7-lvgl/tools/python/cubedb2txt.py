#!/usr/bin/env python3
"""STM32CubeProgrammer 의 Data_Base 를 다운로더용 디바이스 DB 로 바꾼다.

  ./cubedb2txt.py /opt/ST/STM32CubeCLT_1.22.0/STM32CubeProgrammer
  ./cubedb2txt.py <CubeProgrammer 경로> -o assets/sd/prog/mcu/st.txt

왜 PC 에서 하나
  Data_Base 는 XML 104개 5.1MB 다. MCU 에서 읽으려면 XML 파서가 필요하고,
  정작 쓰는 값은 디바이스당 대여섯 개뿐이다. 파이썬은 xml.etree 가 표준이라
  같은 일을 훨씬 싸게 끝낸다.

왜 결과를 저장소에 두나
  변환에 CubeProgrammer 설치가 필요한데, 이 저장소를 받는 사람이 전부 그걸
  깔았을 리 없다. 생성물을 assets/sd/ 에 커밋해두면 --sync 한 번으로 끝난다.

출력 형식은 벤더 중립이다. id_addr/id_mask/id_val 3연은 "어느 주소를 읽어 어느
비트가 무엇이면 이 디바이스" 라고만 적으므로, ST 의 DBGMCU 든 Nordic 의 FICR 든
Microchip 의 DSU 든 같은 형식으로 쓴다.
"""

import argparse
import glob
import os
import sys
import xml.etree.ElementTree as ET


# DBGMCU_IDCODE 주소는 DB 에 없다. 시리즈마다 다르고 ST 문서에만 있어서 여기
# 적어둔다. 확실하지 않은 시리즈는 넣지 않는다 — 틀린 주소를 적으면 엉뚱한
# 레지스터를 읽고 운이 나쁘면 다른 칩으로 오인한다. 빠진 항목은 자동 판별만
# 안 될 뿐 이름을 직접 지정하면 그대로 쓸 수 있다.
#
#   실기 확인 : STM32F4 (0x431 을 0xE0042000 에서 읽어 확인)
#   문서 근거 : 나머지는 각 시리즈 레퍼런스 매뉴얼의 DBGMCU 챕터
ID_ADDR = {
    "STM32F0":  0x40015800,
    "STM32F1":  0xE0042000,
    "STM32F2":  0xE0042000,
    "STM32F3":  0xE0042000,
    "STM32F4":  0xE0042000,
    "STM32F7":  0xE0042000,
    "STM32L0":  0x40015800,
    "STM32L1":  0xE0042000,
    "STM32L4":  0xE0042000,
    "STM32G0":  0x40015800,
    "STM32G4":  0xE0042000,
    "STM32C0":  0x40015800,
    "STM32H7":  0x5C001000,
    "STM32WB":  0xE0042000,
    "STM32WL":  0xE0042000,
}

ID_MASK = 0x00000FFF

# 시리즈 표가 맞지 않는 것으로 실기 확인된 디바이스. 이름에 이 문자열이 들어가면
# id_addr 를 넣지 않는다.
#
#   STM32H7RS : Series 가 STM32H7 라 0x5C001000 이 들어가는데, 실기에서 그 주소도
#               0x44002000 / 0xE0044000 / 0x40015800 도 전부 0 이거나 FAULT 였다.
#               틀린 주소를 적어두면 엉뚱한 레지스터를 읽는다. 이름을 직접
#               지정하면 그대로 쓸 수 있으므로 비워두는 편이 낫다.
NO_ID_ADDR = ("H7RS",)

# DBGMCU 주소를 모를 때 쓰는 대안. DPv2 의 TARGETID 에서 읽는다.
#
#   STM32H7S3 실측 : TARGETID = 0x14850041
#                    TPARTNO[27:12] = 0x4850 = DEV_ID 0x485 << 4
#                    TDESIGNER[11:1] = 0x020 = ST (JEP106)
#
# TPARTNO 만 비교한다. 이 인코딩은 관측 하나에 기댄 것이라 다른 파트에서 다를 수
# 있는데, 값이 정확히 맞아야 하므로 틀리면 "판별 실패" 지 오인은 아니다.
# DPv1 파트에는 TARGETID 자체가 없어서 그 항목은 그냥 맞지 않는다.
ID_TARGETID   = 0xFFFFFFFF      # 펌웨어의 DEV_ID_TARGETID 센티널
TARGETID_MASK = 0x0FFFF000


def dev_id(text):
    """'0x431' / '0x01E' / '0x485_swv' 를 정수로. 뒤에 붙은 꼬리표는 버린다."""
    s = text.strip().split("_")[0].split("-")[0]
    try:
        return int(s, 16 if s.lower().startswith("0x") else 10)
    except ValueError:
        return None


def find_sram(dev):
    """Embedded SRAM 의 시작 주소와 크기. 알고리즘 아레나를 놓을 자리다.

    .FLM 에도 .stldr 에도 없는 유일한 값이라 DB 를 쓰는 가장 큰 이유가 이것이다.
    """
    for per in dev.iter("Peripheral"):
        if per.findtext("Name") != "Embedded SRAM":
            continue
        for cfg in per.iter("Configuration"):
            p = cfg.find("Parameters")
            if p is None:
                continue
            try:
                return int(p.get("address"), 16), int(p.get("size"), 16)
            except (TypeError, ValueError):
                pass
    return None, None


def find_flash(dev):
    """Embedded Flash 의 시작 주소와 크기. 알고리즘 말과 교차 검증하는 용도다.

    알고리즘 파일이 자기 크기를 틀리게 적어 놓은 경우가 실제로 있다 (GigaDevice
    팩의 1MB/2MB .FLM 이 둘 다 3840KB 라고 한다). 그러면 범위 검사가 무력해지므로
    다른 출처의 값을 하나 더 들고 있는다.
    """
    for per in dev.iter("Peripheral"):
        if per.findtext("Name") != "Embedded Flash":
            continue
        for cfg in per.iter("Configuration"):
            p = cfg.find("Parameters")
            if p is None:
                continue
            try:
                return int(p.get("address"), 16), int(p.get("size"), 16)
            except (TypeError, ValueError):
                pass
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cubeprog", help="STM32CubeProgrammer 폴더 (Data_Base 와 bin 을 담고 있는)")
    ap.add_argument("-o", "--out", default="assets/sd/prog/mcu/st.txt")
    ap.add_argument("--loader-dir", default="/prog/loaders/st",
                    help="SD 카드에서 FlashLoader 가 놓일 경로 (기본 /prog/loaders/st)")
    args = ap.parse_args()

    db_dir = os.path.join(args.cubeprog, "Data_Base")
    fl_dir = os.path.join(args.cubeprog, "bin", "FlashLoader")

    if not os.path.isdir(db_dir):
        print(f"Data_Base 가 없습니다 : {db_dir}", file=sys.stderr)
        return 1

    # 로더가 실제로 있는 DEV_ID 집합
    loaders = {}
    for f in glob.glob(os.path.join(fl_dir, "*.stldr")):
        base = os.path.basename(f)
        i = dev_id(os.path.splitext(base)[0])
        if i is not None:
            loaders.setdefault(i, base)

    entries = []
    no_addr = []
    no_sram = []

    for path in sorted(glob.glob(os.path.join(db_dir, "STM32_Prog_DB_*.xml"))):
        try:
            dev = ET.parse(path).getroot().find("Device")
        except ET.ParseError:
            print(f"건너뜀 (XML 오류) : {os.path.basename(path)}", file=sys.stderr)
            continue
        if dev is None:
            continue

        did = dev_id(dev.findtext("DeviceID") or "")
        name = (dev.findtext("Name") or "").strip()
        series = (dev.findtext("Series") or "").strip()
        cpu = (dev.findtext("CPU") or "").strip()
        if did is None or not name:
            continue

        ram, ram_sz = find_sram(dev)
        flash, flash_sz = find_flash(dev)
        if ram is None:
            no_sram.append(name)

        addr = ID_ADDR.get(series)
        if addr is not None and any(k in name for k in NO_ID_ADDR):
            addr = None
        if addr is None:
            no_addr.append(series)

        entries.append(dict(id=did, name=name, series=series, cpu=cpu,
                            ram=ram, ram_sz=ram_sz, id_addr=addr,
                            flash=flash, flash_sz=flash_sz,
                            algo=loaders.get(did)))

    # 같은 DEV_ID 를 다른 파일이 두 번 기술하는 경우가 있다 (0x485 와 0x485_swv).
    # 그대로 두면 자동 판별이 "둘 이상 맞음" 으로 막히므로 여기서 합친다.
    seen = {}
    merged = []
    for e in entries:
        k = (e["name"], e["id"])
        if k in seen:
            old = seen[k]
            # 정보가 더 많은 쪽을 남긴다
            if old["ram"] is None and e["ram"] is not None:
                old.update(ram=e["ram"], ram_sz=e["ram_sz"])
            if old["flash"] is None and e["flash"] is not None:
                old.update(flash=e["flash"], flash_sz=e["flash_sz"])
            if not old["algo"] and e["algo"]:
                old["algo"] = e["algo"]
            continue
        seen[k] = e
        merged.append(e)
    dup = len(entries) - len(merged)
    entries = merged

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# STM32 디바이스 DB — 자동 생성물이므로 손으로 고치지 말 것\n")
        f.write("#\n")
        f.write("#   tools/python/cubedb2txt.py 가 STM32CubeProgrammer 의 Data_Base 에서 만든다.\n")
        f.write(f"#   원본 : {os.path.abspath(args.cubeprog)}\n")
        f.write(f"#   항목 : {len(entries)} 개\n")
        f.write("#\n")
        f.write("# id_addr/id_mask/id_val 은 벤더 중립 3연이다. \"어느 주소를 읽어 어느 비트가\n")
        f.write("# 무엇이면 이 디바이스\" 라고만 적으므로 ST 이외의 벤더도 같은 형식을 쓴다.\n")
        f.write("# id_addr 이 0xFFFFFFFF 면 메모리가 아니라 DPv2 의 TARGETID 에서 읽는다.\n")
        f.write("#\n")
        f.write("# flash/flash_sz 는 알고리즘이 들고 있는 값과 교차 검증하는 용도다.\n")
        f.write("# 알고리즘 파일이 자기 크기를 틀리게 적은 경우가 실제로 있다.\n")
        f.write("#\n")
        f.write("# ram/ram_sz 는 알고리즘을 올릴 자리다. .FLM 에도 .stldr 에도 없는 값이라\n")
        f.write("# 이 DB 를 쓰는 가장 큰 이유가 이것이다.\n")
        f.write("\n")

        for e in sorted(entries, key=lambda x: (x["series"], x["id"])):
            f.write(f"[{e['name']}]\n")
            if e["cpu"]:
                f.write(f"cpu     = {e['cpu']}\n")
            if e["id_addr"] is not None:
                f.write(f"id_addr = 0x{e['id_addr']:08X}\n")
                f.write(f"id_mask = 0x{ID_MASK:08X}\n")
                f.write(f"id_val  = 0x{e['id']:08X}\n")
            else:
                # DBGMCU 주소를 모른다. DPv2 TARGETID 로 간다.
                f.write(f"id_addr = 0x{ID_TARGETID:08X}   # DP TARGETID\n")
                f.write(f"id_mask = 0x{TARGETID_MASK:08X}\n")
                f.write(f"id_val  = 0x{(e['id'] << 16) & TARGETID_MASK:08X}\n")
            if e["ram"] is not None:
                f.write(f"ram     = 0x{e['ram']:08X}\n")
                f.write(f"ram_sz  = 0x{e['ram_sz']:X}\n")
            if e["flash"] is not None:
                f.write(f"flash   = 0x{e['flash']:08X}\n")
                f.write(f"flash_sz = 0x{e['flash_sz']:X}\n")
            if e["algo"]:
                f.write(f"algo    = {args.loader_dir}/{e['algo']}\n")
            f.write("\n")

    auto = len(entries)
    tgt  = sum(1 for e in entries if e["id_addr"] is None)
    with_algo = sum(1 for e in entries if e["algo"])
    print(f"{args.out}")
    print(f"  항목        : {len(entries)} 개")
    if dup:
        print(f"  중복 병합   : {dup} 개  (같은 이름·DEV_ID 를 두 파일이 기술)")
    print(f"  자동 판별   : {auto} 개  (DBGMCU {auto - tgt} / DPv2 TARGETID {tgt})")
    print(f"  로더 연결   : {with_algo} 개")
    if no_addr:
        print(f"  TARGETID 로 : {', '.join(sorted(set(no_addr)))}")
    if no_sram:
        print(f"  SRAM 없음   : {len(no_sram)} 개")
    return 0


if __name__ == "__main__":
    sys.exit(main())
