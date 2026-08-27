# Harness Whale Companion — AI 操作说明

## 项目性质

这是一个个人硬件 Demo / 概念验证。目标是把 DeepSeek Harness 的运行状态通过本机插件、Python BLE 桥接器显示到特定 ESP32-C3 小屏设备，并允许设备回答普通单选题。

## 安全边界

1. 不得读取、打印、记录、复制或提交用户的 API Key、密码、Token、私钥。
2. API Key 只能由用户在 `install_companion.py` 的隐藏输入提示中亲自输入。
3. 不得把 API Key 作为 `--api-key` 命令行参数传递。
4. 烧录会整片擦除目标 ESP32-C3。执行前必须只读确认目标串口，并向用户说明会删除旧固件、NVS、配置和存档。
5. 不得修改无关的 Harness 插件、profile 或系统 Python；本项目使用自己的 `.venv`。
6. 不得把本机 IP、用户名、日志、`.dsh` 内容或设备身份数据提交到 Git。

## 推荐部署路线

优先使用 GitHub Release 中的 `Harness-Whale-v1.1.0-Windows-source.zip`。它包含可审阅源码、离线 BLE wheel、烧录器和已校验固件，不含 EXE。

完整步骤见 `docs/AI_DEPLOYMENT.md`。执行顺序必须是：环境检查 → 安装 Companion → 重启 Harness → 查询 `/whale` → 连接目标串口 → 固件校验 → 用户确认 → 烧录 → 验证。

## 源码地图

- `harness/plugin/index.ts`：监听 Harness 事件、发送状态、代理单选、启动桥接器。
- `harness/bridge/harness_ble_bridge.py`：本机 UDP、余额请求、BLE 连接、设备回答回传。
- `harness/bridge/harness_ble_protocol.py`：二进制状态和问题帧。
- `installer/install_companion.py`：安装到当前用户的 `~/.dsh`，不需要管理员权限。
- `installer/flash_firmware.py`：验证 manifest 和 SHA-256 后擦除并烧录。
- `device-firmware/components/app/src/app.c`：固件 Harness-only 运行入口与按键回答。
- `device-firmware/components/ui/presentation/src/ui_harness.c`：UI、中文字体、动效和滚动余额。

## 关键事实

- 电脑和设备用 BLE 连接，不需要同一局域网或 Wi-Fi。
- 桥接器在架构上必需，但安装后由插件自动启动，用户不需要手动常驻一个终端。
- BLE 名称：`HARNESS-WHALE`。
- Harness 查询命令：`/whale`。
- 只支持普通单选题在设备端作答；多选、自由文本和复杂补充返回电脑端。
- 人民币金额格式为 `数字 + 元`，超宽时循环滚动。
- 任务结束会触发立即、3 秒、10 秒、30 秒余额刷新，另有 300 秒定时兜底。
- `PRODUCT_HARNESS_ONLY=1`，设备运行时只进入 Harness 功能；源码树中的其他板级模块是构建依赖，不代表运行时功能。

## 变更后的最低验证

```powershell
python -m unittest discover -s tests -v
python -m compileall -q harness installer tests
npx.cmd --yes esbuild harness/plugin/index.ts --bundle --platform=node --format=esm --outfile=NUL
```

若修改固件协议，还必须同步修改 C 端 `harness_status.*`、Python `harness_ble_protocol.py` 和对应测试。
