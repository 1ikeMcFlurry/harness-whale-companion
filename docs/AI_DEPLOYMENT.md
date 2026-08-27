# AI 自动部署指南

本文给豆包、Codex 或其他可操作 Windows 终端的 AI 使用。不要猜测路径，不要跳过校验，也不要向用户索要明文 API Key。

## 目标

在 Windows 10/11 64 位电脑上：

1. 安装 Harness Whale Companion 插件和 BLE 桥接器；
2. 让用户静默配置可选的 DeepSeek API Key；
3. 把 Release 中经过 SHA-256 校验的合并固件烧录到目标 ESP32-C3；
4. 验证 Harness、桥接器和设备三端连通。

## 不允许做的事

- 不要让用户把 API Key 发到聊天中。
- 不要用 `--api-key` 参数，不要把 Key 放入环境回显、脚本、Markdown 或日志。
- 不要安装到全局 Python；使用发布包脚本创建的独立环境。
- 不要烧录未确认的串口，不要使用 `--no-erase` 绕过标准流程。
- 不要修改 `~/.dsh` 中与本 Demo 无关的插件。

## 路线 A：使用 Release 发布包（推荐）

### A1. 只读检查

```powershell
python --version
python -c "import struct; print(struct.calcsize('P') * 8)"
Get-PnpDevice -Class Bluetooth | Select-Object Status,FriendlyName
```

通过条件：Python 3.10～3.13、64 位、Windows 蓝牙可用。若条件不满足，停止并告诉用户缺少什么。

### A2. 检查发布包

解压 `Harness-Whale-v1.1.0-Windows-source.zip`，确认至少有：

```text
install_companion.py
flash_firmware.py
requirements-flash.txt
payload/index.ts
payload/harness_ble_bridge.py
payload/harness_ble_protocol.py
firmware/Harness-Whale-ESP32C3-8MB.bin
firmware/manifest.json
wheels/
```

### A3. 安装

在发布包根目录运行：

```powershell
python install_companion.py
```

脚本出现 API Key 提示时，把终端控制权交还用户，让用户亲自输入；AI 不得读取输入。用户可以直接回车跳过余额。

安装成功后要求用户完全退出并重启 Harness。不要手动长期启动 `harness_ble_bridge.py`，插件会自动管理它。

### A4. 连接验证

在 Harness 输入：

```text
/whale
```

预期能看到 bridge 状态。若设备已经开机，稍后应显示连接到 `HARNESS-WHALE`。

### A5. 烧录前检查

用 USB 数据线连接设备，先只读列出串口：

```powershell
python -c "from serial.tools import list_ports; [print(p.device, p.description, p.vid, p.pid) for p in list_ports.comports()]"
```

如果 `serial` 还未安装，先创建烧录环境，再执行同一环境下的检查：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r requirements-flash.txt
.\.flash-venv\Scripts\python.exe -c "from serial.tools import list_ports; [print(p.device, p.description, p.vid, p.pid) for p in list_ports.comports()]"
```

向用户明确说明：下一步会整片擦除选定 ESP32-C3 的旧固件、NVS、配置和存档。获得确认后再继续。

### A6. 校验并烧录

交互运行，让脚本再次确认串口和固件：

```powershell
.\.flash-venv\Scripts\python.exe flash_firmware.py
```

不要使用隐藏的 `--no-erase`。烧录器会根据 `manifest.json` 校验 SHA-256，从 `0x0` 写入完整合并固件。

### A7. 最终验证

1. 设备重启并显示鲸鱼娘界面；
2. Windows 能发现 BLE 名称 `HARNESS-WHALE`；
3. Harness `/whale` 显示 connected；
4. 新建任务后，设备标题、计时和状态发生变化；
5. 用一个普通单选问题验证上/下/确认键；
6. 若配置了 API Key，任务结束后观察余额在 0～30 秒内刷新。

## 路线 B：从 Git 仓库安装

```powershell
git clone https://github.com/1ikeMcFlurry/harness-whale-companion.git
cd harness-whale-companion
python installer\install_companion.py
```

仓库安装器联网安装 `bleak`。预编译固件不提交到 Git 历史，请从 Release 同时下载 `.bin` 和 `manifest.json` 并放在同一目录。

创建烧录环境：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe installer\flash_firmware.py --firmware D:\absolute\path\Harness-Whale-ESP32C3-8MB.bin
```

## 幂等性与恢复

- 重复运行安装器是安全的：插件和桥接源码会被更新，受管 profile 块会原位替换。
- profile 修改前会在同目录生成带时间戳的 `.whale-backup-*` 备份。
- 安装失败时保留原 Harness 配置，阅读错误后修复依赖并重跑。
- 烧录失败时不要换错串口；先检查 USB 数据线、Boot 模式和设备供电，再重新执行同一烧录命令。
