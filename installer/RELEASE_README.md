# Harness Whale Companion v1.1.0

这是个人制作的 DeepSeek Harness 硬件状态副屏 Demo 发布包。

包内只有可审阅的 Python / TypeScript 源码、Python wheel 和 ESP32-C3 合并固件，不包含 EXE。

## 第一步：安装电脑端

需要 64 位 Windows 10/11 与 Python 3.10～3.13。在本目录运行：

```powershell
python install_companion.py
```

安装器会安装 Harness 插件与 BLE 桥接器、创建独立虚拟环境、离线安装 BLE 依赖并更新 Harness profile。API Key 由用户在终端中静默输入，只用于显示余额；直接回车可以跳过。

完成后完全退出并重新启动 DeepSeek Harness。输入 `/whale` 查看桥接器与设备状态。桥接器由插件自动启动，无需手动常驻终端。

## 第二步：烧录设备

固件只适用于本 Demo 已测试的 ESP32-C3 8 MB、ST7789P3 240×320、三 ADC 按键板级配置。

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r requirements-flash.txt
.\.flash-venv\Scripts\python.exe flash_firmware.py
```

烧录器会校验 SHA-256、自动选择串口、整片擦除并从 `0x0` 写入完整固件。整片擦除会删除设备上的旧固件、NVS、配置和存档。

烧录后 BLE 名称为 `HARNESS-WHALE`。普通单选题可在设备上用上/下键切换、确认键提交；多选和自由文本仍在电脑端完成。

完整源码、架构、协议、AI 自动部署和排障文档：

https://github.com/1ikeMcFlurry/harness-whale-companion
