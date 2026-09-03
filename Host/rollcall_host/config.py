"""Holding the tunable constants for the roll-call host application.

Machine-specific values — the paired Bluetooth COM port above all — live in
a gitignored `.env` file next to this package's `Host/` directory rather
than being hardcoded here, so switching machines or re-pairing the HC-06
module never touches a tracked source file. `.env.example` documents every
key with its default; copy it to `.env` and edit the values that differ on
your machine. A key left unset in `.env` falls back to the default below.
"""

import os

from dotenv import load_dotenv

load_dotenv()

SERIAL_PORT = os.environ.get("SERIAL_PORT", "COM3")
"""Naming the virtual COM port exposed by the paired HC-06 module.

Adjusting this per machine: check Device Manager (Windows) or `/dev/rfcomm*`
(Linux) after pairing, since the assigned port number is not guaranteed to
stay the same across computers or re-pairings. Overridden via the
`SERIAL_PORT` key in `.env`.
"""

BAUD_RATE = int(os.environ.get("BAUD_RATE", "9600"))
"""Matching the USART3 baud rate configured on the STM32 firmware side."""

SERIAL_TIMEOUT_SECONDS = float(os.environ.get("SERIAL_TIMEOUT_SECONDS", "1.0"))
"""Bounding each read call so the listener periodically regains control to
check for shutdown requests instead of blocking forever on `readline()`.
"""

RECONNECT_DELAY_SECONDS = float(os.environ.get("RECONNECT_DELAY_SECONDS", "3.0"))
"""Waiting this long before retrying after a failed or dropped serial
connection, so a missing device does not spin the CPU in a tight loop.
"""

DB_PATH = os.environ.get("DB_PATH", "rollcall.db")
"""Storing the SQLite database file next to the process's working directory
by default; set the `DB_PATH` key in `.env` to override this for a specific
deployment.
"""
