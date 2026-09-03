"""Providing the SQLite schema and data-access functions backing the
`card_uid` <-> `student_id` lookup table described in `SA.md` section 4.

Two tables are maintained: `cards` (one row per bound card, written once at
registration time) and `attendance` (one row per check-in, appended every
time a already-bound card is read).
"""

import sqlite3
from typing import Optional


def get_connection(db_path: str) -> sqlite3.Connection:
    """Opening a SQLite connection with row access by column name.

    Enabling `sqlite3.Row` as the row factory so callers can read fields as
    `row["student_id"]` instead of relying on positional indices.
    """
    connection = sqlite3.connect(db_path)
    connection.row_factory = sqlite3.Row
    return connection


def init_db(connection: sqlite3.Connection) -> None:
    """Creating the `cards` and `attendance` tables if they do not exist yet.

    Calling this once at startup keeps a fresh database file self-initializing
    without requiring a separate migration step.
    """
    connection.execute(
        """
        CREATE TABLE IF NOT EXISTS cards (
            card_uid     TEXT PRIMARY KEY,
            student_id   TEXT NOT NULL,
            student_name TEXT,
            bound_at     TEXT NOT NULL
        )
        """
    )
    connection.execute(
        """
        CREATE TABLE IF NOT EXISTS attendance (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            card_uid   TEXT NOT NULL,
            student_id TEXT NOT NULL,
            checked_at TEXT NOT NULL,
            FOREIGN KEY (card_uid) REFERENCES cards (card_uid)
        )
        """
    )
    connection.commit()


def find_card(connection: sqlite3.Connection, card_uid: str) -> Optional[sqlite3.Row]:
    """Looking up a card by UID and returning its bound row, or `None` when
    the UID has never been registered.
    """
    cursor = connection.execute(
        "SELECT card_uid, student_id, student_name, bound_at FROM cards WHERE card_uid = ?",
        (card_uid,),
    )
    return cursor.fetchone()


def bind_card(
    connection: sqlite3.Connection,
    card_uid: str,
    student_id: str,
    student_name: Optional[str],
    bound_at: str,
) -> None:
    """Registering a previously-unseen card by writing its first UID <->
    student_id binding into the `cards` table.
    """
    connection.execute(
        "INSERT INTO cards (card_uid, student_id, student_name, bound_at) VALUES (?, ?, ?, ?)",
        (card_uid, student_id, student_name, bound_at),
    )
    connection.commit()


def record_attendance(
    connection: sqlite3.Connection,
    card_uid: str,
    student_id: str,
    checked_at: str,
) -> None:
    """Appending one attendance record for an already-bound card."""
    connection.execute(
        "INSERT INTO attendance (card_uid, student_id, checked_at) VALUES (?, ?, ?)",
        (card_uid, student_id, checked_at),
    )
    connection.commit()
