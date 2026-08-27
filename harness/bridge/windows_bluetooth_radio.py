#!/usr/bin/env python3
"""Inspect or enable the Windows Bluetooth radio through WinRT."""

from __future__ import annotations

import argparse
import asyncio


async def run(enable: bool) -> int:
    try:
        from winrt.windows.devices.radios import Radio, RadioKind, RadioState
    except ImportError as exc:
        raise SystemExit("Windows radio WinRT packages are not installed") from exc

    access = await Radio.request_access_async()
    radios = await Radio.get_radios_async()
    bluetooth = [radio for radio in radios if radio.kind == RadioKind.BLUETOOTH]
    print(f"ACCESS={access.name} BLUETOOTH_RADIOS={len(bluetooth)}")
    for index, radio in enumerate(bluetooth):
        print(f"RADIO_{index}_NAME={radio.name} STATE={radio.state.name}")
        if enable and radio.state != RadioState.ON:
            result = await radio.set_state_async(RadioState.ON)
            print(f"RADIO_{index}_SET={result.name} STATE_AFTER={radio.state.name}")
    return 0 if bluetooth and all(radio.state == RadioState.ON for radio in bluetooth) else 1


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enable", action="store_true")
    args = parser.parse_args()
    raise SystemExit(asyncio.run(run(args.enable)))


if __name__ == "__main__":
    main()
