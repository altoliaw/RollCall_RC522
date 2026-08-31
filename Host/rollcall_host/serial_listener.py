"""Listening on the virtual COM port for `CARD:XXXXXX` packets sent by the
STM32 firmware and yielding the parsed UID strings to the caller.

Isolating all pyserial and reconnect handling here keeps `cli.py` free to
focus purely on the registration/attendance decision logic.
"""

import re
import time
from typing import Iterator

import serial

_CARD_LINE_PATTERN = re.compile(r"^CARD:([0-9A-Fa-f]+)\s*$")
"""Matching a well-formed card packet and capturing the hex UID payload."""


def read_card_uids(
    port: str,
    baud_rate: int,
    timeout_seconds: float,
    reconnect_delay_seconds: float,
) -> Iterator[str]:
    """Yielding one uppercase hex UID string per valid `CARD:XXXXXX` line
    read from the serial port, forever.

    Reopening the connection automatically, after waiting
    `reconnect_delay_seconds`, whenever the port fails to open or drops
    mid-stream, so a loose cable or a not-yet-paired Bluetooth link does not
    crash the whole listener.
    """
    while True:
        try:
            with serial.Serial(port, baud_rate, timeout=timeout_seconds) as connection:
                print(f"Connected to {port} at {baud_rate} baud.")
                yield from _read_lines(connection)
        except serial.SerialException as error:
            print(f"Serial error on {port}: {error}. Retrying in {reconnect_delay_seconds:.0f}s.")
            time.sleep(reconnect_delay_seconds)


def _read_lines(connection: "serial.Serial") -> Iterator[str]:
    """Reading lines from an already-open connection until it drops,
    yielding only the UID payload of lines that match `CARD:XXXXXX`.

    Raising `serial.SerialException` (letting it propagate to the caller)
    when a read fails, so the outer loop in `read_card_uids` can reconnect.
    """
    while True:
        raw_line = connection.readline()
        if not raw_line:
            continue  # Treating a read timeout with no data as a normal poll, not an error.

        decoded_line = raw_line.decode("ascii", errors="replace").strip()
        if not decoded_line:
            continue

        match = _CARD_LINE_PATTERN.match(decoded_line)
        if match is None:
            print(f"Ignoring malformed line: {decoded_line!r}")
            continue

        yield match.group(1).upper()
