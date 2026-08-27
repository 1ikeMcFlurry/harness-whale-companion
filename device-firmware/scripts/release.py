#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
trae_card 精简固件发布脚本(单变体)。

做的事(移植自 folotoy release.py 的主干,去掉多变体矩阵/littlefs/OTA/上传):
  1. idf.py build
  2. idf.py merge-bin  → 合并 bootloader + 分区表 + app + 内置头像资源
     (cardid 身份 / imgstore·imgframe / nvs 仍为每台或运行时数据)
  3. 打成带 版本 + git hash + 分支渠道 的 zip 放 releases/

用法:
  python scripts/release.py            # 构建 + 打包(默认)
  python scripts/release.py --no-build # 跳过构建,直接用现有 build/ 打包
分支渠道:main → release(无后缀);其它分支 → beta(带 hash,自动 -beta.N)。
"""
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
RELEASES = os.path.join(ROOT, "releases")
BOARD_CFG = os.path.join(ROOT, "components", "platform", "platform_esp32",
                         "include", "platform", "board_config.h")
CHIP = "esp32c3"


def validate_avatar_release_inputs(build_dir):
    """拒绝用缺少内置头像烧录项的旧构建目录生成发布包。"""
    build_dir = Path(build_dir)
    required = ("imgava.bin", "avatar_catalog.json", "flash_args")
    for name in required:
        path = build_dir / name
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"发布包缺少有效的 {name},请先执行 idf.py build")

    flash_args = (build_dir / "flash_args").read_text(encoding="utf-8").split()
    if "imgava.bin" not in flash_args:
        raise RuntimeError(
            "build/flash_args 未包含 imgava.bin,这是旧构建缓存;"
            "请执行 idf.py fullclean 后重新运行发布脚本"
        )


def run(cmd, **kw):
    print("· " + " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True, **kw)


def git(*args, default=""):
    try:
        return subprocess.check_output(["git", *args], cwd=ROOT,
                                       stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return default


def read_flash_size():
    """从 sdkconfig 读 flash 大小(如 8MB),用于命名与合并镜像。"""
    try:
        with open(os.path.join(ROOT, "sdkconfig")) as f:
            m = re.search(r'CONFIG_ESPTOOLPY_FLASHSIZE="([^"]+)"', f.read())
            return m.group(1) if m else "unknown"
    except Exception:
        return "unknown"


def read_periph_tag():
    """读 board_config.h 的 PERIPH_* 开关,拼成变体标签(反映这份镜像开了哪些外设)。"""
    short = {"DISPLAY": "disp", "AUDIO": "aud", "BUTTON": "btn",
             "BLE": "ble", "BATTERY": "bat", "LED": "led"}
    on = []
    with open(BOARD_CFG) as f:
        for line in f:
            m = re.match(r"\s*#define\s+PERIPH_(\w+)\s+(\d)", line)
            if m and m.group(2) == "1" and m.group(1) in short:
                on.append(short[m.group(1)])
    order = ["disp", "aud", "btn", "ble", "bat", "led"]
    on.sort(key=lambda x: order.index(x) if x in order else 99)
    return "-".join(on) if on else "none"


def main():
    do_build = "--no-build" not in sys.argv

    if do_build:
        run(["idf.py", "build"])
    if not os.path.isdir(BUILD):
        sys.exit("✗ 没有 build/,请先构建(去掉 --no-build)")
    try:
        validate_avatar_release_inputs(BUILD)
    except RuntimeError as exc:
        sys.exit(f"✗ {exc}")

    # 版本/身份信息
    with open(os.path.join(BUILD, "project_description.json")) as f:
        desc = json.load(f)
    version = desc.get("project_version") or "0.0.0"
    app_bin = desc.get("app_bin", "trae_card.bin")
    target = desc.get("target", CHIP)
    flash_size = read_flash_size()
    periph = read_periph_tag()

    branch = git("rev-parse", "--abbrev-ref", "HEAD", default="nogit")
    short_hash = git("rev-parse", "--short", "HEAD", default="0000000")
    dirty = "+dirty" if git("status", "--porcelain") else ""
    channel = "release" if branch == "main" else "beta"

    os.makedirs(RELEASES, exist_ok=True)

    # 合并单文件出厂镜像(boot + 分区表 + app + imgava 内置资源)。
    # 直接调 esptool merge_bin:flash_args 里是 "--flash_size detect",而 merge_bin 不认 detect,
    # 用 sdkconfig 的实际大小(如 8MB)替换。段/偏移沿用 build/flash_args。
    merged = os.path.join(BUILD, "merged-firmware.bin")
    with open(os.path.join(BUILD, "flash_args")) as f:
        fa = f.read().split()
    for i, tok in enumerate(fa):
        if tok == "detect":
            fa[i] = flash_size if flash_size != "unknown" else "8MB"
    esptool = "esptool.py" if shutil.which("esptool.py") else None
    merge_cmd = ([esptool] if esptool else [sys.executable, "-m", "esptool"]) + \
        ["--chip", target, "merge_bin", "-o", "merged-firmware.bin", "-f", "raw"] + fa
    print("· " + " ".join(merge_cmd))
    subprocess.run(merge_cmd, cwd=BUILD, check=True)

    # 命名:trae_card_<target>_<flash>_<periph>_<channel>[-beta.N]_<hash>[+dirty]_v<version>
    base = f"trae_card_{target}_{flash_size}_{periph}_{channel}_{short_hash}{dirty}_v{version}"
    if channel == "beta":
        n = 1
        while os.path.exists(os.path.join(RELEASES, f"{base}-beta.{n}.zip")):
            n += 1
        base = f"{base}-beta.{n}"
    zip_path = os.path.join(RELEASES, base + ".zip")
    if os.path.exists(zip_path):
        sys.exit(f"✗ 同名 zip 已存在(改版本或删旧包): {zip_path}")

    # manifest:说明这份镜像是什么 + 烧录方法 + 每台数据须另行处理
    manifest = f"""trae_card 固件发布包
================================
版本 (PROJECT_VER) : {version}
芯片 / Flash       : {target} / {flash_size}
外设变体 (PERIPH)  : {periph}   (未开: led)
分支 / 渠道         : {branch} / {channel}
Git                : {short_hash}{dirty}

一键出厂烧录(单文件,从 0x0 写起):
  esptool.py --chip {target} write_flash --flash_mode dio --flash_freq 80m --flash_size detect 0x0 merged-firmware.bin

  ⚠ 必须带 --flash_size detect:合并镜像的 bootloader 头里 flash 大小是打包时按
    sdkconfig 写死的({flash_size});detect 会在烧录时按实际芯片重新探测并回写该头,
    否则芯片实际 flash ≠ {flash_size}(换料/不同批次)时 bootloader 初始化 flash 失败,不启动。
    (等价于 idf.py flash 的默认行为 —— 分立烧录能起、合并烧录不起,差别就在这一步。)

⚠ merged-firmware.bin 含 bootloader + 分区表 + app + imgava 内置头像。以下为每台/运行时数据,不在镜像内:
  - cardid   : 出厂身份(SN/Key),每台不同,用产线台账/串口指令单独写入
  - imgstore : 全屏图,运行时经 BLE 上传
  - imgframe : 头像/全屏图解码帧,运行时生成
  - nvs      : 首次开机自动创建
分立 bin 与 flasher_args.json 也一并打包,便于按分区单独烧录。
"""

    # 组包:合并镜像 + 分立 bin + flasher_args.json + manifest
    items = [
        (merged, "merged-firmware.bin"),
        (os.path.join(BUILD, "bootloader", "bootloader.bin"), "bootloader.bin"),
        (os.path.join(BUILD, "partition_table", "partition-table.bin"), "partition-table.bin"),
        (os.path.join(BUILD, app_bin), app_bin),
        (os.path.join(BUILD, "imgava.bin"), "imgava.bin"),
        (os.path.join(BUILD, "avatar_catalog.json"), "avatar_catalog.json"),
        (os.path.join(BUILD, "flasher_args.json"), "flasher_args.json"),
    ]
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in items:
            if os.path.exists(src):
                z.write(src, arc)
            else:
                print(f"  (跳过缺失: {arc})")
        z.writestr("MANIFEST.txt", manifest)

    size_kb = os.path.getsize(zip_path) // 1024
    print("\n✓ 发布包已生成:")
    print(f"  {zip_path}  ({size_kb} KB)")
    print(f"  变体: {target} / {flash_size} / PERIPH={periph} / {channel} / {short_hash}{dirty} / v{version}")
    if dirty:
        print("  ⚠ 工作区有未提交改动(+dirty),正式发布建议先提交干净再打包")


if __name__ == "__main__":
    main()
