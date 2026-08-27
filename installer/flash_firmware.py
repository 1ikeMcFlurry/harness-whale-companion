#!/usr/bin/env python3
"""Source one-command flasher for the merged Harness Whale firmware."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import sys


ESPRESSIF_VID = 0x303A
FIRMWARE_NAME = "Harness-Whale-ESP32C3-8MB.bin"


def configure_console() -> None:
    if os.name == "nt":
        try:
            ctypes.windll.kernel32.SetConsoleOutputCP(65001)
            ctypes.windll.kernel32.SetConsoleCP(65001)
        except (AttributeError, OSError):
            pass
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")


def app_resource(*parts: str) -> Path:
    return Path(__file__).resolve().parent.joinpath(*parts)


def firmware_path(name: str) -> Path:
    return app_resource("firmware", name)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_payload(path: Path) -> dict[str, object]:
    manifest_path = path.with_name("manifest.json")
    if not manifest_path.is_file():
        raise FileNotFoundError("固件清单缺失，请确认 .bin 与仓库中的 manifest.json 位于同一目录")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected = manifest.get("sha256", {})
    if not path.is_file():
        raise FileNotFoundError(f"固件资源缺失：{path.name}")
    if sha256_file(path) != expected.get(path.name):
        raise RuntimeError(f"固件校验失败：{path.name}")
    return manifest


def candidate_ports() -> list[object]:
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    preferred = [port for port in ports if port.vid == ESPRESSIF_VID]
    return preferred or ports


def choose_port(requested: str | None, *, non_interactive: bool = False) -> str:
    if requested:
        return requested
    ports = candidate_ports()
    if not ports:
        raise RuntimeError("没有检测到串口。请用 USB 数据线连接设备后重试")
    if len(ports) == 1:
        return str(ports[0].device)
    if non_interactive:
        raise RuntimeError("检测到多个串口，请使用 --port 指定")
    print("检测到多个串口：")
    for index, port in enumerate(ports, start=1):
        print(f"  {index}. {port.device}  {port.description}")
    while True:
        value = input("请选择设备序号：").strip()
        if value.isdigit() and 1 <= int(value) <= len(ports):
            return str(ports[int(value) - 1].device)


def invoke_esptool(arguments: list[str]) -> None:
    import esptool
    try:
        result = esptool.main(arguments)
    except SystemExit as exc:
        if exc.code not in (None, 0):
            raise RuntimeError(f"esptool 失败，退出码 {exc.code}") from exc
    else:
        if isinstance(result, int) and result != 0:
            raise RuntimeError(f"esptool 失败，退出码 {result}")


def flash(port: str, path: Path, *, erase: bool = True) -> None:
    common = ["--chip", "esp32c3", "--port", port, "--baud", "460800"]
    if erase:
        invoke_esptool(common + ["erase_flash"])
    write = common + [
        "write_flash", "--flash_mode", "dio", "--flash_freq", "80m",
        "--flash_size", "detect", "0x0", str(path),
    ]
    invoke_esptool(write)


def run(args: argparse.Namespace) -> int:
    print("\nDeepSeek Harness 鲸鱼状态副屏 · 一键烧录\n")
    path = Path(args.firmware).expanduser().resolve() if args.firmware else firmware_path(FIRMWARE_NAME)
    manifest = verify_payload(path)
    port = choose_port(args.port, non_interactive=args.yes)
    print(f"固件版本：{manifest.get('version', 'unknown')}")
    print(f"目标串口：{port}")
    print("注意：本工具会擦除设备现有内容，并写入 Harness 专用固件。")
    if not args.yes:
        input("确认设备已连接，按回车开始烧录……")
    if args.dry_run:
        print("[检查] 固件校验与串口识别通过；dry-run 未写入设备")
        return 0
    flash(port, path, erase=not args.no_erase)
    print("\n[成功] 固件烧录和校验完成。设备将自动重启。")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--firmware", help="合并固件 .bin 路径")
    parser.add_argument("--yes", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-erase", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> None:
    configure_console()
    try:
        args = parse_args()
        result = run(args)
        raise SystemExit(result)
    except KeyboardInterrupt:
        print("\n已取消烧录。")
        raise SystemExit(130)
    except Exception as exc:
        print(f"\n[失败] {exc}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
