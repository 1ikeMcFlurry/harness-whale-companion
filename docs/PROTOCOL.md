# BLE 协议

协议实现在 `harness/bridge/harness_ble_protocol.py`，固件对应解析在 `device-firmware/components/core/services/src/harness_status.c`。

## GATT

| 方向 | UUID | 用途 |
|---|---|---|
| 电脑 → 设备 | `54524145-4341-5244-0000-000000000010` | 状态帧、问题帧 |
| 设备 → 电脑 | `54524145-4341-5244-0000-000000000011` | 按键选择通知 |

## 通用帧头

```text
byte 0      frame_version = 1
byte 1      message_type
byte 2..3   payload_length, little-endian
byte 4..    payload
```

`message_type=0x07` 是状态，`0x08` 是单选问题。

## 状态 payload v2

固定区使用 little-endian：

```text
u8  payload_version = 2
u8  state
u8  tool
u8  flags
u32 seq
u32 elapsed_seconds
u16 todo_done
u16 todo_total
u32 balance_minor
u8  currency
u8  title_utf8_length
u8[] title_utf8, 最多 72 bytes
```

状态值：`0 offline`、`1 idle`、`2 thinking`、`3 tool`、`4 waiting`、`5 done`、`6 error`、`7 stopped`、`8 question`。

工具值：`0 none`、`1 terminal`、`2 read`、`3 edit`、`4 search`、`5 web`、`6 task`、`255 other`。

flags：bit 0 表示包含余额，bit 1 表示余额可用，bit 2 表示新任务。currency：`0 none`、`1 CNY`、`2 USD`。

## 问题 payload v1

```text
u8 question_version = 1
u8 option_count, 最大 4
repeat option_count:
  u8 label_utf8_length
  u8[] label_utf8, 每项最多 36 bytes
```

超过一页的选择由桥接器拆成“3 个真实选项 + 1 个翻页项”。固件通知 `0x40 + visible_index`，桥接器再映射回完整选项数组。

## 文本兼容

标题和选项在电脑端先按 GB2312 可表示范围清洗，再按 UTF-8 字节上限安全截断，避免半个多字节字符和设备缺字。固件内含 16 px 与 24 px 中文字库。
