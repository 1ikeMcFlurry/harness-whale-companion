#!/usr/bin/env python3
"""Relay local DeepSeek Harness events to the passport over BLE.

The Harness plugin talks only to this process on 127.0.0.1 via UDP. The bridge
does not forward prompts, file paths, tool arguments, outputs, or API keys.
"""

from __future__ import annotations

import argparse
import asyncio
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import json
import logging
import os
import time
from typing import Any
import urllib.error
import urllib.request

try:
    from .harness_ble_protocol import (
        Balance, Currency, State, make_frame, make_question_frame, normalize_state, snapshot_from_mapping,
    )
except ImportError:  # Direct execution: python tools/harness_ble_bridge.py
    from harness_ble_protocol import (
        Balance, Currency, State, make_frame, make_question_frame, normalize_state, snapshot_from_mapping,
    )


CMD_UUID = "54524145-4341-5244-0000-000000000010"
NOTIFY_UUID = "54524145-4341-5244-0000-000000000011"
DEFAULT_DEVICE_NAME = "HARNESS-WHALE"
DEFAULT_UDP_PORT = 8765
DEFAULT_TELEMETRY_PORT = 8766
BALANCE_URL = "https://api.deepseek.com/user/balance"
FINAL_HOLD_SECONDS = 4.0
STALE_EVENT_SECONDS = 30.0
QUESTION_SINGLE_PAGE_OPTIONS = 4
QUESTION_PAGED_OPTIONS = 3

LOG = logging.getLogger("harness-ble")


def read_deepseek_api_key() -> str:
    """Read the key without ever forwarding it to Harness, UDP, or BLE.

    The Windows installer writes the value to the current user's Environment
    registry key. Reading that key directly also works when Harness was already
    running before the value was configured and therefore has a stale process
    environment.
    """
    value = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if value or os.name != "nt":
        return value
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Environment") as key:
            stored, _ = winreg.QueryValueEx(key, "DEEPSEEK_API_KEY")
        return str(stored).strip()
    except (FileNotFoundError, OSError, TypeError, ValueError):
        return ""


def parse_balance_document(document: dict[str, Any]) -> Balance | None:
    infos = document.get("balance_infos")
    if not isinstance(infos, list) or not infos:
        return None
    candidates = [item for item in infos if isinstance(item, dict)]
    candidates.sort(key=lambda item: str(item.get("currency", "")).upper() != "CNY")
    for item in candidates:
        code = str(item.get("currency", "")).upper()
        currency = Currency.CNY if code == "CNY" else Currency.USD if code == "USD" else None
        if currency is None:
            continue
        try:
            minor = int((Decimal(str(item.get("total_balance", "0"))) * 100)
                        .quantize(Decimal("1"), rounding=ROUND_HALF_UP))
        except (InvalidOperation, TypeError, ValueError):
            continue
        return Balance(max(0, minor), currency, bool(document.get("is_available", False)))
    return None


def fetch_balance(api_key: str) -> Balance | None:
    request = urllib.request.Request(
        BALANCE_URL,
        headers={
            "Accept": "application/json",
            "Authorization": f"Bearer {api_key}",
            "Cache-Control": "no-cache",
            "Pragma": "no-cache",
        },
        method="GET",
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        document = json.loads(response.read().decode("utf-8"))
    if not isinstance(document, dict):
        return None
    return parse_balance_document(document)


class StatusModel:
    def __init__(self) -> None:
        self.current: dict[str, Any] = {"state": "offline", "tool": "none"}
        self.pending: dict[str, Any] | None = None
        self.updated_at = 0.0
        self.final_until = 0.0
        self.seq = 0
        self.balance: Balance | None = None
        self.balance_refresh = asyncio.Event()
        self.new_turn_pending = False
        self.changed = asyncio.Event()
        self.question: dict[str, Any] | None = None
        self.question_addr: tuple[str, int] | None = None
        self.question_page = 0
        self.question_revision = 0
        self.datagram_transport: asyncio.DatagramTransport | None = None

    def merge(self, data: dict[str, Any]) -> None:
        if data.get("v") != 1:
            return
        now = time.monotonic()
        incoming = dict(data)
        state = normalize_state(incoming.get("state"))
        if state in (State.DONE, State.ERROR, State.STOPPED):
            self.current = incoming
            self.pending = None
            self.final_until = now + FINAL_HOLD_SECONDS
        elif normalize_state(self.current.get("state")) in (State.DONE, State.ERROR, State.STOPPED) and now < self.final_until:
            self.pending = incoming
        else:
            self.current = incoming
            self.pending = None
        if incoming.get("newTurn"):
            self.new_turn_pending = True
        self.updated_at = now
        self.changed.set()

    def set_balance(self, value: Balance | None) -> None:
        if value != self.balance:
            self.balance = value
            self.changed.set()

    def request_balance_refresh(self) -> None:
        self.balance_refresh.set()

    def set_question(self, data: dict[str, Any], addr: tuple[str, int]) -> None:
        request_id = str(data.get("requestId") or "")
        options = data.get("options")
        if not request_id or not isinstance(options, list) or len(options) < 2:
            return
        labels = [str(item) for item in options]
        self.question = {"requestId": request_id, "options": labels}
        self.question_addr = addr
        self.question_page = 0
        self.question_revision += 1
        self.changed.set()

    def clear_question(self, request_id: str = "") -> None:
        if self.question is None:
            return
        if request_id and request_id != self.question.get("requestId"):
            return
        self.question = None
        self.question_addr = None
        self.question_page = 0
        self.question_revision += 1
        self.changed.set()

    def question_page_view(self) -> tuple[list[str], int, int | None]:
        """Return visible labels, their source offset and the optional paging index."""
        if self.question is None:
            return [], 0, None
        options = self.question["options"]
        if len(options) <= QUESTION_SINGLE_PAGE_OPTIONS:
            return list(options), 0, None
        page_count = (len(options) + QUESTION_PAGED_OPTIONS - 1) // QUESTION_PAGED_OPTIONS
        page = self.question_page % page_count
        start = page * QUESTION_PAGED_OPTIONS
        labels = list(options[start:start + QUESTION_PAGED_OPTIONS])
        labels.append("下一页" if page + 1 < page_count else "回到开头")
        return labels, start, len(labels) - 1

    def answer_question(self, index: int) -> None:
        pending = self.question
        addr = self.question_addr
        transport = self.datagram_transport
        if pending is None or addr is None or transport is None:
            return
        visible, start, paging_index = self.question_page_view()
        if not 0 <= index < len(visible):
            return
        if paging_index is not None and index == paging_index:
            page_count = ((len(pending["options"]) + QUESTION_PAGED_OPTIONS - 1) //
                          QUESTION_PAGED_OPTIONS)
            self.question_page = (self.question_page + 1) % page_count
            self.question_revision += 1
            self.changed.set()
            return
        selected = start + index
        if selected >= len(pending["options"]):
            return
        document = {
            "v": 1,
            "kind": "harness-answer",
            "requestId": pending["requestId"],
            "selected": selected,
        }
        transport.sendto(json.dumps(document, separators=(",", ":")).encode("utf-8"), addr)
        self.clear_question(str(pending["requestId"]))

    def question_frame(self) -> bytes:
        visible, _, _ = self.question_page_view()
        return make_question_frame(visible)

    def next_frame(self) -> bytes:
        now = time.monotonic()
        if self.pending is not None and now >= self.final_until:
            self.current = self.pending
            self.pending = None
        data = dict(self.current)
        state = normalize_state(data.get("state"))
        if self.updated_at and now - self.updated_at > STALE_EVENT_SECONDS and state not in (State.IDLE, State.OFFLINE):
            data.update(state="idle", tool="none")
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        snapshot = snapshot_from_mapping(
            data,
            seq=self.seq,
            balance=self.balance,
            new_turn=self.new_turn_pending,
        )
        self.new_turn_pending = False
        return make_frame(snapshot)


class HarnessDatagram(asyncio.DatagramProtocol):
    def __init__(self, model: StatusModel) -> None:
        self.model = model

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.model.datagram_transport = transport  # type: ignore[assignment]

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if addr[0] not in {"127.0.0.1", "::1"} or len(data) > 8192:
            return
        try:
            document = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            LOG.warning("ignored malformed local Harness event")
            return
        if isinstance(document, dict):
            kind = document.get("kind")
            if kind == "harness-question":
                self.model.set_question(document, addr)
            elif kind == "harness-question-cancel":
                self.model.clear_question(str(document.get("requestId") or ""))
            elif kind == "harness-balance-refresh":
                self.model.request_balance_refresh()
            else:
                self.model.merge(document)


async def balance_loop(model: StatusModel, interval: float) -> None:
    api_key = read_deepseek_api_key()
    if not api_key:
        LOG.info("DEEPSEEK_API_KEY is not set; balance will display as --")
        return
    while True:
        model.balance_refresh.clear()
        try:
            value = await asyncio.to_thread(fetch_balance, api_key)
            model.set_balance(value)
            LOG.info("DeepSeek balance refreshed (%s)", value.currency.name if value else "unavailable")
        except (OSError, urllib.error.URLError, ValueError, json.JSONDecodeError) as exc:
            LOG.warning("balance refresh failed: %s", type(exc).__name__)
        try:
            await asyncio.wait_for(model.balance_refresh.wait(), timeout=interval)
        except asyncio.TimeoutError:
            pass


async def write_frame(client: Any, frame: bytes) -> None:
    mtu = getattr(client, "mtu_size", 23) or 23
    step = max(20, min(mtu - 3, 244))
    for offset in range(0, len(frame), step):
        await client.write_gatt_char(CMD_UUID, frame[offset:offset + step], response=True)


def send_telemetry(transport: asyncio.DatagramTransport, port: int, **fields: Any) -> None:
    document = {
        "v": 1,
        "kind": "harness-whale-hardware",
        "sentAt": int(time.time() * 1000),
        **fields,
    }
    transport.sendto(json.dumps(document, separators=(",", ":")).encode("utf-8"),
                     ("127.0.0.1", port))


async def ble_loop(model: StatusModel, device_name: str, address: str | None,
                   interval: float, telemetry: asyncio.DatagramTransport,
                   telemetry_port: int) -> None:
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as exc:
        raise SystemExit("bleak is required; install tools/requirements-client.txt") from exc

    packets_sent = 0
    while True:
        try:
            device = address
            if device is None:
                LOG.info("scanning for BLE device %s", device_name)
                send_telemetry(telemetry, telemetry_port, connected=False,
                               state="scanning", device=device_name,
                               packetsSent=packets_sent)
                found = await BleakScanner.find_device_by_name(device_name, timeout=8.0)
                if found is None:
                    send_telemetry(telemetry, telemetry_port, connected=False,
                                   state="not-found", device=device_name,
                                   packetsSent=packets_sent)
                    await asyncio.sleep(3)
                    continue
                device = found
            send_telemetry(telemetry, telemetry_port, connected=False,
                           state="connecting", device=device_name,
                           packetsSent=packets_sent)
            async with BleakClient(device) as client:
                LOG.info("BLE connected to %s", device_name)
                send_telemetry(telemetry, telemetry_port, connected=True,
                               state="connected", device=device_name,
                               mtu=int(getattr(client, "mtu_size", 23) or 23),
                               packetsSent=packets_sent)

                def on_notify(_: Any, data: bytearray) -> None:
                    if len(data) >= 2 and data[0] == 0x07 and data[1] >= 0x10:
                        LOG.warning("device rejected Harness packet: status=0x%02X", data[1])
                    if len(data) >= 2 and data[0] == 0x08 and 0x40 <= data[1] < 0x44:
                        model.answer_question(data[1] - 0x40)

                await client.start_notify(NOTIFY_UUID, on_notify)
                question_revision = -1
                while client.is_connected:
                    model.changed.clear()
                    if question_revision != model.question_revision:
                        await write_frame(client, model.question_frame())
                        question_revision = model.question_revision
                    await write_frame(client, model.next_frame())
                    packets_sent += 1
                    send_telemetry(telemetry, telemetry_port, connected=True,
                                   state="connected", device=device_name,
                                   mtu=int(getattr(client, "mtu_size", 23) or 23),
                                   packetsSent=packets_sent)
                    try:
                        await asyncio.wait_for(model.changed.wait(), timeout=interval)
                    except asyncio.TimeoutError:
                        pass
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOG.warning("BLE reconnect after %s: %s", type(exc).__name__, exc)
            send_telemetry(telemetry, telemetry_port, connected=False,
                           state="reconnecting", device=device_name,
                           error=type(exc).__name__, packetsSent=packets_sent)
            await asyncio.sleep(3)


async def demo_loop(model: StatusModel) -> None:
    demo = [
        {"v": 1, "state": "idle", "tool": "none", "elapsed": 0},
        {"v": 1, "state": "thinking", "tool": "none", "elapsed": 2, "newTurn": True},
        {"v": 1, "state": "tool", "tool": "Read", "elapsed": 5, "todoDone": 1, "todoTotal": 4},
        {"v": 1, "state": "tool", "tool": "exec_command", "elapsed": 8, "todoDone": 2, "todoTotal": 4},
        {"v": 1, "state": "waiting", "tool": "none", "elapsed": 11, "todoDone": 2, "todoTotal": 4},
        {"v": 1, "state": "question", "tool": "none", "elapsed": 13,
         "_options": ["继续修复", "先看说明", "稍后处理", "其他(电脑)"]},
        {"v": 1, "state": "stopped", "tool": "none", "elapsed": 14, "todoDone": 2, "todoTotal": 4},
        {"v": 1, "state": "done", "tool": "none", "elapsed": 16, "todoDone": 4, "todoTotal": 4},
        {"v": 1, "state": "idle", "tool": "none", "elapsed": 16, "todoDone": 4, "todoTotal": 4},
    ]
    while True:
        for item in demo:
            current = dict(item)
            options = current.pop("_options", None)
            if options is None:
                model.clear_question("demo")
            else:
                model.set_question({"requestId": "demo", "options": options}, ("127.0.0.1", 9))
            model.merge(current)
            await asyncio.sleep(4)


async def async_main(args: argparse.Namespace) -> None:
    loop = asyncio.get_running_loop()
    model = StatusModel()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: HarnessDatagram(model), local_addr=("127.0.0.1", args.port))
    telemetry, _ = await loop.create_datagram_endpoint(
        lambda: asyncio.DatagramProtocol(), local_addr=("127.0.0.1", 0))
    LOG.info("listening for Harness events on 127.0.0.1:%d", args.port)
    send_telemetry(telemetry, args.telemetry_port, connected=False,
                   state="starting", device=args.device_name, packetsSent=0)
    tasks = []
    try:
        if args.demo:
            tasks.append(asyncio.create_task(demo_loop(model)))
        if not args.no_balance:
            tasks.append(asyncio.create_task(balance_loop(model, args.balance_interval)))
        if args.dry_run:
            while True:
                model.changed.clear()
                LOG.info("dry-run frame=%s", model.next_frame().hex())
                try:
                    await asyncio.wait_for(model.changed.wait(), timeout=args.interval)
                except asyncio.TimeoutError:
                    pass
        else:
            await ble_loop(model, args.device_name, args.address, args.interval,
                           telemetry, args.telemetry_port)
    finally:
        send_telemetry(telemetry, args.telemetry_port, connected=False,
                       state="stopped", device=args.device_name)
        transport.close()
        telemetry.close()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="DeepSeek Harness to BLE passport bridge")
    parser.add_argument("--device-name", default=DEFAULT_DEVICE_NAME)
    parser.add_argument("--address", help="optional fixed BLE address")
    parser.add_argument("--port", type=int, default=DEFAULT_UDP_PORT)
    parser.add_argument("--telemetry-port", type=int, default=DEFAULT_TELEMETRY_PORT)
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--balance-interval", type=float, default=300.0)
    parser.add_argument("--no-balance", action="store_true")
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    try:
        asyncio.run(async_main(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
