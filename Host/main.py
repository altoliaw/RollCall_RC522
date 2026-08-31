"""Serving as the entry point for the roll-call host application.

Run from this directory:
    pip install -r requirements.txt
    copy .env.example .env   # then edit SERIAL_PORT etc. for this machine
    python main.py
"""

from rollcall_host.cli import run

if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        print("\nStopped by user.")
