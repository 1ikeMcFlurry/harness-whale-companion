#!/usr/bin/env python3
"""Source installer for the DeepSeek Harness Whale companion."""

from __future__ import annotations

import argparse
import ctypes
from datetime import datetime
import getpass
import json
import os
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import urllib.error
import urllib.request


PLUGIN_ID = "harness-whale-companion"
BEGIN_MARKER = "# BEGIN HARNESS WHALE COMPANION"
END_MARKER = "# END HARNESS WHALE COMPANION"
BALANCE_URL = "https://api.deepseek.com/user/balance"


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


def source_resource(name: str) -> Path:
    packaged = app_resource("payload", name)
    if packaged.is_file():
        return packaged
    project = Path(__file__).resolve().parents[1]
    candidates = {
        "index.ts": project / "harness" / "plugin" / "index.ts",
        "harness_ble_bridge.py": project / "harness" / "bridge" / "harness_ble_bridge.py",
        "harness_ble_protocol.py": project / "harness" / "bridge" / "harness_ble_protocol.py",
    }
    return candidates[name]


def plugin_block(plugin_url: str) -> str:
    return (
        f"{BEGIN_MARKER}\n"
        "- insert:\n"
        f"  - id: {PLUGIN_ID}\n"
        f"    name: '{plugin_url}'\n"
        f"{END_MARKER}\n"
    )


def patch_profile_text(current: str, plugin_url: str) -> tuple[str, bool]:
    block = plugin_block(plugin_url)
    if BEGIN_MARKER in current and END_MARKER in current:
        start = current.index(BEGIN_MARKER)
        end = current.index(END_MARKER, start) + len(END_MARKER)
        if end < len(current) and current[end] == "\n":
            end += 1
        updated = current[:start] + block + current[end:]
        return updated, updated != current

    lines = current.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.strip() != f"- id: {PLUGIN_ID}":
            continue
        for candidate in range(index + 1, min(index + 5, len(lines))):
            if lines[candidate].lstrip().startswith("name:"):
                indent = lines[candidate][:len(lines[candidate]) - len(lines[candidate].lstrip())]
                newline = "\r\n" if lines[candidate].endswith("\r\n") else "\n"
                lines[candidate] = f"{indent}name: '{plugin_url}'{newline}"
                updated = "".join(lines)
                return updated, updated != current
        return current, False

    separator = "" if not current or current.endswith(("\n", "\r")) else "\n"
    return current + separator + block, True


def patch_profile(path: Path, plugin_url: str, *, dry_run: bool = False) -> bool:
    current = path.read_text(encoding="utf-8") if path.exists() else ""
    updated, changed = patch_profile_text(current, plugin_url)
    if not changed or dry_run:
        return changed
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        shutil.copy2(path, path.with_name(f"{path.name}.whale-backup-{stamp}"))
    path.write_text(updated, encoding="utf-8", newline="")
    return True


def profile_paths(dsh_root: Path) -> list[Path]:
    profiles = dsh_root / "profiles"
    # Harness profiles are direct children (for example profiles/web). Never
    # patch package templates under profiles/node_modules.
    found = sorted(profiles.glob("*/cordis.patch.yml")) if profiles.exists() else []
    return found or [profiles / "web" / "cordis.patch.yml"]


def validate_api_key(api_key: str) -> tuple[bool | None, str]:
    request = urllib.request.Request(
        BALANCE_URL,
        headers={"Accept": "application/json", "Authorization": f"Bearer {api_key}"},
        method="GET",
    )
    try:
        with urllib.request.urlopen(request, timeout=12) as response:
            document = json.loads(response.read().decode("utf-8"))
        infos = document.get("balance_infos", []) if isinstance(document, dict) else []
        currencies = ", ".join(
            str(item.get("currency", "")) for item in infos if isinstance(item, dict)
        )
        return True, f"余额接口验证成功{f'（{currencies}）' if currencies else ''}"
    except urllib.error.HTTPError as exc:
        if exc.code in (401, 403):
            return False, "API Key 无效或没有余额接口权限"
        return None, f"余额接口暂时返回 HTTP {exc.code}，已保留配置"
    except (OSError, ValueError, json.JSONDecodeError):
        return None, "暂时无法联网验证 API Key，已保留配置"


def set_user_api_key(api_key: str) -> None:
    if os.name != "nt":
        raise RuntimeError("一键安装器目前仅支持 Windows")
    import winreg
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Environment") as key:
        winreg.SetValueEx(key, "DEEPSEEK_API_KEY", 0, winreg.REG_SZ, api_key)
    try:
        HWND_BROADCAST = 0xFFFF
        WM_SETTINGCHANGE = 0x001A
        SMTO_ABORTIFHUNG = 0x0002
        result = ctypes.c_size_t()
        ctypes.windll.user32.SendMessageTimeoutW(
            HWND_BROADCAST, WM_SETTINGCHANGE, 0, "Environment",
            SMTO_ABORTIFHUNG, 5000, ctypes.byref(result),
        )
    except (AttributeError, OSError):
        pass


def install_payload(dsh_root: Path, *, dry_run: bool = False) -> tuple[Path, Path, Path]:
    plugin_source = source_resource("index.ts")
    bridge_source = source_resource("harness_ble_bridge.py")
    protocol_source = source_resource("harness_ble_protocol.py")
    if not plugin_source.is_file() or not bridge_source.is_file() or not protocol_source.is_file():
        raise FileNotFoundError("源码资源不完整，请重新下载项目或发布包")

    plugin_dest = dsh_root / "plugins" / PLUGIN_ID / "index.ts"
    bridge_dir = dsh_root / "harness-whale-bridge"
    bridge_dest = bridge_dir / "harness_ble_bridge.py"
    protocol_dest = bridge_dir / "harness_ble_protocol.py"
    if not dry_run:
        plugin_dest.parent.mkdir(parents=True, exist_ok=True)
        bridge_dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(plugin_source, plugin_dest)
        shutil.copy2(bridge_source, bridge_dest)
        shutil.copy2(protocol_source, protocol_dest)
    return plugin_dest, bridge_dest, protocol_dest


def ensure_bridge_venv(bridge_dir: Path, *, dry_run: bool = False) -> Path:
    venv_dir = bridge_dir / ".venv"
    python = venv_dir / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if dry_run:
        return python
    if not python.is_file():
        subprocess.run([sys.executable, "-m", "venv", str(venv_dir)], check=True)
    wheels = app_resource("wheels")
    install = [str(python), "-m", "pip", "install", "--disable-pip-version-check"]
    if wheels.is_dir() and any(wheels.glob("bleak-*.whl")):
        if os.name != "nt" or struct.calcsize("P") != 8 or not ((3, 10) <= sys.version_info[:2] <= (3, 13)):
            raise RuntimeError(
                "发布包的离线 wheel 支持 64 位 Windows Python 3.10～3.13；"
                "请换用受支持版本，或从源码目录联网安装"
            )
        install.extend(["--no-index", "--find-links", str(wheels), "bleak==1.1.1"])
    else:
        install.append("bleak==1.1.1")
    subprocess.run(install, check=True)
    return python


def run(args: argparse.Namespace) -> int:
    dsh_root = Path(args.dsh_root).expanduser().resolve() if args.dsh_root else Path.home() / ".dsh"
    print("\nDeepSeek Harness 鲸鱼状态副屏 · 源码安装\n")
    plugin_dest, bridge_dest, _ = install_payload(dsh_root, dry_run=args.dry_run)
    python = ensure_bridge_venv(bridge_dest.parent, dry_run=args.dry_run)
    plugin_url = plugin_dest.resolve().as_uri()

    changed_profiles = []
    for profile in profile_paths(dsh_root):
        if patch_profile(profile, plugin_url, dry_run=args.dry_run):
            changed_profiles.append(profile)

    api_key = args.api_key
    if api_key is None and not args.skip_key and not args.non_interactive:
        api_key = getpass.getpass("请输入自己的 DeepSeek API Key（直接回车可跳过余额）：").strip()
    if api_key:
        ok, message = validate_api_key(api_key)
        print(f"[{'成功' if ok else '提示'}] {message}")
        if ok is False:
            print("未保存无效的 API Key。")
        elif not args.dry_run:
            set_user_api_key(api_key)

    print(f"[完成] Harness 插件：{plugin_dest}")
    print(f"[完成] BLE 转发源码：{bridge_dest}")
    print(f"[完成] 独立 Python 环境：{python}")
    for profile in changed_profiles:
        print(f"[完成] 已更新 profile：{profile}")
    if not changed_profiles:
        print("[完成] Harness profile 已经是最新配置")
    if args.dry_run:
        print("[检查] dry-run 未修改任何文件")
    else:
        print("\n请完全退出并重新启动 DeepSeek Harness。之后无需手动运行桥接器。")
        print("在 Harness 中输入 /whale 可查看硬件连接状态。")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dsh-root", help="测试或自定义 .dsh 目录")
    parser.add_argument("--api-key", help=argparse.SUPPRESS)
    parser.add_argument("--skip-key", action="store_true")
    parser.add_argument("--non-interactive", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> None:
    configure_console()
    try:
        args = parse_args()
        result = run(args)
        raise SystemExit(result)
    except KeyboardInterrupt:
        print("\n已取消安装。")
        raise SystemExit(130)
    except Exception as exc:
        print(f"\n[失败] {exc}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
