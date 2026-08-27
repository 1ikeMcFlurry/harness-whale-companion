# AI 自动部署指南

本文给豆包、Codex 或其他可操作 Windows 终端的 AI 使用。代码、安装器、manifest 和说明都在 GitHub 仓库中；Release 只提供一个已经编译好的 `.bin` 固件。

## 目标

在 Windows 10/11 64 位电脑上：

1. 下载代码仓库和 Release 固件；
2. 安装 Harness Whale Companion 插件与 BLE 桥接器；
3. 让用户静默配置可选的 DeepSeek API Key；
4. 校验并烧录目标 ESP32-C3；
5. 验证 Harness、桥接器和设备三端连通。

## 不允许做的事

- 不要让用户把 API Key 发到聊天中。
- 不要用 `--api-key` 参数，不要把 Key 放入环境回显、脚本、Markdown 或日志。
- 不要安装到全局 Python；使用项目或安装器创建的独立 `.venv`。
- 不要烧录未确认的串口，不要使用隐藏的 `--no-erase` 绕过标准流程。
- 不要修改 `~/.dsh` 中与本 Demo 无关的插件。
- 不要把本机 IP、用户名、`.dsh` 内容或完整日志提交到 Git。

## 1. 获取代码

```powershell
git clone https://github.com/1ikeMcFlurry/harness-whale-companion.git
cd harness-whale-companion
```

如果没有 Git，可以从 GitHub 使用 **Code → Download ZIP** 并解压。后续命令都在仓库根目录运行。

## 2. 只读环境检查

```powershell
python --version
python -c "import struct; print(struct.calcsize('P') * 8)"
Get-PnpDevice -Class Bluetooth | Select-Object Status,FriendlyName
```

通过条件：Python 3.10～3.13、64 位、Windows 蓝牙可用。若条件不满足，停止并告诉用户缺少什么。

确认源码存在：

```text
installer/install_companion.py
installer/flash_firmware.py
installer/requirements-flash.txt
installer/firmware/manifest.json
harness/plugin/index.ts
harness/bridge/harness_ble_bridge.py
harness/bridge/harness_ble_protocol.py
```

## 3. 安装 Companion

```powershell
python installer\install_companion.py
```

脚本出现 API Key 提示时，把终端控制权交还用户，让用户亲自输入；AI 不得读取输入。用户可以直接回车跳过余额。

安装器将：

- 安装插件到 `~/.dsh/plugins/harness-whale-companion/`；
- 安装桥接器到 `~/.dsh/harness-whale-bridge/`；
- 创建桥接器专用 `.venv` 并安装 `bleak`；
- 幂等更新 Harness profile。

安装成功后要求用户完全退出并重启 Harness。不要手动长期启动 `harness_ble_bridge.py`，插件会自动管理它。

## 4. 检查 Harness 连接

在 Harness 输入：

```text
/whale
```

预期能看到 bridge 状态。设备已经开机时，稍后应显示连接到 `HARNESS-WHALE`。

## 5. 下载唯一的 Release 固件

从以下 Release 页面下载 `Harness-Whale-ESP32C3-8MB.bin`：

https://github.com/1ikeMcFlurry/harness-whale-companion/releases/latest

把文件放到：

```text
installer/firmware/Harness-Whale-ESP32C3-8MB.bin
```

不要修改文件名。`installer/firmware/manifest.json` 中记录了预期 SHA-256，烧录器会强制校验。

## 6. 创建烧录环境并只读检查串口

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe -c "from serial.tools import list_ports; [print(p.device, p.description, p.vid, p.pid) for p in list_ports.comports()]"
```

确认目标串口属于待烧录 ESP32-C3。若存在多个串口，不要猜；让用户确认具体 `COMx`。

向用户明确说明：下一步会整片擦除选定设备的旧固件、NVS、配置和存档。获得确认后再继续。

## 7. 校验并烧录

单一明确串口可使用：

```powershell
.\.flash-venv\Scripts\python.exe installer\flash_firmware.py --port COMx
```

也可不传 `--port`，由脚本列出并交互选择。不要使用 `--yes` 跳过人工确认，也不要使用隐藏的 `--no-erase`。

烧录器会读取仓库中的 manifest、校验 SHA-256、擦除 Flash，并从 `0x0` 写入完整镜像。

## 8. 最终验证

1. 设备重启并显示鲸鱼娘界面；
2. Windows 能发现 BLE 名称 `HARNESS-WHALE`；
3. Harness `/whale` 显示 connected；
4. 新建任务后，设备标题、计时和状态发生变化；
5. 用一个普通单选问题验证上/下/确认键；
6. 若配置了 API Key，任务结束后观察余额在 0～30 秒内刷新。

## 幂等性与恢复

- 重复运行安装器是安全的：插件和桥接源码会被更新，受管 profile 块会原位替换。
- profile 修改前会在同目录生成带时间戳的 `.whale-backup-*` 备份。
- 安装失败时保留原 Harness 配置，阅读错误后修复依赖并重跑。
- 烧录失败时不要换错串口；先检查 USB 数据线、Boot 模式和设备供电，再重新执行同一命令。
