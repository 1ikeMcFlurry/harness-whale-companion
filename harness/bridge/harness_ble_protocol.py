"""Wire helpers for the compact DeepSeek Harness BLE status message."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
from typing import Any, Mapping


FRAME_VERSION = 1
MESSAGE_TYPE = 0x07
QUESTION_MESSAGE_TYPE = 0x08
PAYLOAD_VERSION = 2
PAYLOAD_BASE_SIZE = 21
TITLE_MAX_BYTES = 72
QUESTION_MAX_OPTIONS = 4
QUESTION_LABEL_MAX_BYTES = 36


class State(IntEnum):
    OFFLINE = 0
    IDLE = 1
    THINKING = 2
    TOOL = 3
    WAITING = 4
    DONE = 5
    ERROR = 6
    STOPPED = 7
    QUESTION = 8


class Tool(IntEnum):
    NONE = 0
    TERMINAL = 1
    READ = 2
    EDIT = 3
    SEARCH = 4
    WEB = 5
    TASK = 6
    OTHER = 0xFF


class Currency(IntEnum):
    NONE = 0
    CNY = 1
    USD = 2


FLAG_HAS_BALANCE = 1 << 0
FLAG_BALANCE_AVAILABLE = 1 << 1
FLAG_NEW_TURN = 1 << 2


STATE_BY_NAME = {member.name.lower(): member for member in State}


@dataclass(frozen=True)
class Balance:
    minor: int
    currency: Currency
    available: bool = True


@dataclass(frozen=True)
class Snapshot:
    state: State = State.OFFLINE
    tool: Tool = Tool.NONE
    seq: int = 0
    elapsed_s: int = 0
    todo_done: int = 0
    todo_total: int = 0
    balance: Balance | None = None
    new_turn: bool = False
    title: str = ""


def _bounded(value: Any, low: int, high: int) -> int:
    try:
        value = int(value)
    except (TypeError, ValueError):
        value = low
    return max(low, min(high, value))


def normalize_state(value: Any) -> State:
    if isinstance(value, State):
        return value
    return STATE_BY_NAME.get(str(value).strip().lower(), State.OFFLINE)


def normalize_tool(value: Any) -> Tool:
    if isinstance(value, Tool):
        return value
    name = str(value or "").strip().lower()
    if not name or name in {"none", "--"}:
        return Tool.NONE
    if any(part in name for part in ("terminal", "shell", "exec", "command", "bash", "powershell")):
        return Tool.TERMINAL
    if any(part in name for part in ("web", "browser", "http", "crawl")):
        return Tool.WEB
    if any(part in name for part in ("read", "open", "view", "fetch")):
        return Tool.READ
    if any(part in name for part in ("edit", "write", "patch", "replace", "create")):
        return Tool.EDIT
    if any(part in name for part in ("search", "find", "grep", "glob")):
        return Tool.SEARCH
    if any(part in name for part in ("task", "todo", "agent", "plan")):
        return Tool.TASK
    return Tool.OTHER


def snapshot_from_mapping(data: Mapping[str, Any], *, seq: int,
                          balance: Balance | None = None,
                          new_turn: bool | None = None) -> Snapshot:
    total = _bounded(data.get("todoTotal", data.get("todo_total", 0)), 0, 0xFFFF)
    done = _bounded(data.get("todoDone", data.get("todo_done", 0)), 0, total)
    state = normalize_state(data.get("state"))
    tool = normalize_tool(data.get("tool"))
    if state != State.TOOL:
        tool = Tool.NONE
    return Snapshot(
        state=state,
        tool=tool,
        seq=_bounded(seq, 0, 0xFFFFFFFF),
        elapsed_s=_bounded(data.get("elapsed", data.get("elapsed_s", 0)), 0, 0xFFFFFFFF),
        todo_done=done,
        todo_total=total,
        balance=balance,
        new_turn=bool(data.get("newTurn", data.get("new_turn", False)))
        if new_turn is None else bool(new_turn),
        title=str(data.get("title") or "").strip(),
    )


def _truncate_utf8(value: str, limit: int) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) <= limit:
        return raw
    raw = raw[:limit]
    while raw:
        try:
            raw.decode("utf-8")
            return raw
        except UnicodeDecodeError:
            raw = raw[:-1]
    return b""


def sanitize_display_text(value: Any) -> str:
    """Keep only glyphs guaranteed by the firmware's ASCII + GB2312 font."""
    return str(value or "").encode("gb2312", errors="replace").decode("gb2312")


def pack_payload(snapshot: Snapshot) -> bytes:
    flags = FLAG_NEW_TURN if snapshot.new_turn else 0
    balance_minor = 0
    currency = Currency.NONE
    if snapshot.balance is not None:
        flags |= FLAG_HAS_BALANCE
        if snapshot.balance.available:
            flags |= FLAG_BALANCE_AVAILABLE
        balance_minor = _bounded(snapshot.balance.minor, 0, 0xFFFFFFFF)
        currency = snapshot.balance.currency
        if currency not in (Currency.CNY, Currency.USD):
            raise ValueError("balance currency must be CNY or USD")
    base = struct.pack(
        "<BBBBIIHHIB",
        PAYLOAD_VERSION,
        int(snapshot.state),
        int(snapshot.tool),
        flags,
        _bounded(snapshot.seq, 0, 0xFFFFFFFF),
        _bounded(snapshot.elapsed_s, 0, 0xFFFFFFFF),
        _bounded(snapshot.todo_done, 0, 0xFFFF),
        _bounded(snapshot.todo_total, 0, 0xFFFF),
        balance_minor,
        int(currency),
    )
    if len(base) != PAYLOAD_BASE_SIZE:
        raise AssertionError(f"unexpected Harness base size: {len(base)}")
    title = _truncate_utf8(sanitize_display_text(snapshot.title), TITLE_MAX_BYTES)
    return base + bytes((len(title),)) + title


def make_frame(snapshot: Snapshot) -> bytes:
    payload = pack_payload(snapshot)
    return bytes((FRAME_VERSION, MESSAGE_TYPE)) + len(payload).to_bytes(2, "little") + payload


def pack_question_payload(options: list[Any] | tuple[Any, ...]) -> bytes:
    if len(options) > QUESTION_MAX_OPTIONS:
        raise ValueError(f"at most {QUESTION_MAX_OPTIONS} hardware options are supported")
    payload = bytearray((1, len(options)))
    for index, option in enumerate(options, start=1):
        text = sanitize_display_text(option).strip() or f"选项 {index}"
        encoded = _truncate_utf8(text, QUESTION_LABEL_MAX_BYTES)
        payload.append(len(encoded))
        payload.extend(encoded)
    return bytes(payload)


def make_question_frame(options: list[Any] | tuple[Any, ...]) -> bytes:
    payload = pack_question_payload(options)
    return bytes((FRAME_VERSION, QUESTION_MESSAGE_TYPE)) + len(payload).to_bytes(2, "little") + payload
