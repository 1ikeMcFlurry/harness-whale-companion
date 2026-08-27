#!/usr/bin/env python3
"""Capture one 240x320 device screenshot over BLE and save it as PNG."""

from __future__ import annotations

import argparse
import queue
import sys
import time
from pathlib import Path

from ble_card_client import BleWorker
from harness_ble_protocol import Balance, Currency, Snapshot, State, Tool, pack_payload
from screen_capture_protocol import rgb565le_to_image


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
        sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="BLE address of the card")
    parser.add_argument("output", type=Path, help="output PNG path")
    parser.add_argument("--timeout", type=float, default=75.0)
    parser.add_argument(
        "--harness-state",
        choices=[member.name.lower() for member in State],
        help="inject a demo Harness snapshot before capture",
    )
    args = parser.parse_args()

    events: queue.Queue[tuple[str, object]] = queue.Queue()

    def on_event(kind: str, payload: object) -> None:
        if kind == "log":
            print(payload, flush=True)
        if kind in {"capture_complete", "capture_error"}:
            events.put((kind, payload))

    worker = BleWorker(on_event)
    try:
        worker.submit(worker._connect(args.address)).result(timeout=15)
        if args.harness_state:
            state = State[args.harness_state.upper()]
            tool = Tool.READ if state == State.TOOL else Tool.NONE
            snapshot = Snapshot(
                state=state,
                tool=tool,
                seq=1,
                elapsed_s=18,
                todo_done=2,
                todo_total=5,
                balance=Balance(minor=2834, currency=Currency.CNY),
                new_turn=state == State.THINKING,
                title="把鲸鱼娘放进状态副屏",
            )
            worker.submit(worker._write_frame(0x07, pack_payload(snapshot))).result(timeout=10)
            time.sleep(0.75)
        worker.start_capture()
        kind, payload = events.get(timeout=args.timeout)
        if kind == "capture_error":
            raise RuntimeError(str(payload))

        complete = payload
        image = rgb565le_to_image(complete.data, complete.width, complete.height)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        image.save(args.output, "PNG")
        print(f"saved {complete.width}x{complete.height}: {args.output}", flush=True)

        worker.finish_capture(complete.capture_id)
        time.sleep(0.5)
        return 0
    except (TimeoutError, queue.Empty) as exc:
        print(f"capture timed out: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"capture failed: {exc}", file=sys.stderr)
        return 1
    finally:
        try:
            worker.submit(worker._disconnect()).result(timeout=10)
        except Exception:
            pass
        worker.loop.call_soon_threadsafe(worker.loop.stop)


if __name__ == "__main__":
    raise SystemExit(main())
