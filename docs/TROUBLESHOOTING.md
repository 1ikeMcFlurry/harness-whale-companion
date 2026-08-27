# 排障手册

按“Harness 插件 → 桥接器 → Windows 蓝牙 → 设备”的顺序检查，不要一开始就重复烧录。

## `/whale` 不存在

1. 确认安装器显示成功更新 `cordis.patch.yml`。
2. 完全退出 Harness，包括后台进程，再重新启动。
3. 重新运行 `python install_companion.py`；它是幂等的。
4. 检查 `~/.dsh/plugins/harness-whale-companion/index.ts` 是否存在。

## bridge offline

检查以下文件：

```text
~/.dsh/harness-whale-bridge/harness_ble_bridge.py
~/.dsh/harness-whale-bridge/harness_ble_protocol.py
~/.dsh/harness-whale-bridge/.venv/Scripts/python.exe
```

可临时手动运行桥接器查看错误；诊断完成后关闭它，让插件重新管理进程：

```powershell
& "$HOME\.dsh\harness-whale-bridge\.venv\Scripts\python.exe" `
  "$HOME\.dsh\harness-whale-bridge\harness_ble_bridge.py" --verbose
```

## scanning 但不 connected

- 设备必须已烧录本 Demo 固件并开机，BLE 名称为 `HARNESS-WHALE`。
- 打开 Windows 蓝牙；不要求电脑和设备接入 Wi-Fi。
- 关闭可能占用 BLE 连接的测试脚本、手机 App 或第二个 Harness 实例。
- 让设备靠近电脑后重启设备和 Harness。

## 余额为 `--` 或异常

重新运行安装器并在隐藏提示中输入自己的 DeepSeek API Key。Key 不要发到聊天或命令行。任务结束的刷新是立即请求，再在 3、10、30 秒补拉；账单侧若延迟入账，最终仍有 5 分钟刷新。

## 计时停在 0～1 秒

使用最新插件与固件。计时基于任务 `startedAt` 计算，设备在运行状态下会用最后接收时间本地递增；新任务才归零。若仍异常，先确认 `/whale` 持续 connected，避免桥接器反复重连导致状态被重置。

## 设备端不能选择

仅普通单选会发送到设备。多选、自由文本、缺少两个有效选项或结构不完整的问题会保留在电脑端。普通单选时用上/下切换、确认提交；电脑先回答后，设备问题会被取消。

## 中文缺字或乱码

- 优先使用 Release 固件。
- 自编译时保留 `lv_font_cn_16.c`、`lv_font_cn_24.c`。
- 电脑端会把 GB2312 之外字符替换为 `?`，这是为了避免整行加载失败。

## 烧录器找不到串口

- 使用可传输数据的 USB 线，而不是仅充电线。
- 在设备管理器确认串口存在。
- 若有多个串口，用 `--port COMx` 明确指定。
- 某些板子需要按住 Boot 再短按 Reset 进入下载模式。

烧录会整片擦除；不要在不确定串口对应设备时尝试。
