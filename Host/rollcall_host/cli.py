"""Driving the main roll-call loop: reading UIDs from the serial listener,
looking each one up in the database, and branching into either the
registration flow or the attendance flow per the "Option 1" decision
recorded in `RC522Test.md` section 9.

MCU firmware does not need to know which flow is in play; every decision
here happens after the packet has already been received.
"""

import sqlite3
from datetime import datetime

from . import config, db
from .serial_listener import read_card_uids


def _prompt_for_student_id() -> str:
    """Reading a non-empty, whitespace-free student ID from the console,
    re-prompting until the input passes validation.
    """
    while True:
        raw_input_value = input("未登記卡片，請輸入學號: ").strip()
        if raw_input_value and raw_input_value.isalnum():
            return raw_input_value
        print("學號格式錯誤（不可為空、只能是英數字），請重新輸入。")


def _prompt_for_student_name() -> str:
    """Reading an optional display name for a newly-bound card; an empty
    answer is accepted since `student_name` is optional in the schema.
    """
    return input("學生姓名（選填，直接按 Enter 略過）: ").strip() or None


def _handle_registration(connection: sqlite3.Connection, card_uid: str) -> None:
    """Running the first-time-swipe registration flow for an unbound UID:
    collecting a student ID (and optional name) and writing the binding.
    """
    student_id = _prompt_for_student_id()
    student_name = _prompt_for_student_name()
    bound_at = datetime.now().isoformat(timespec="seconds")

    try:
        db.bind_card(connection, card_uid, student_id, student_name, bound_at)
    except sqlite3.Error as error:
        print(f"寫入對照表失敗（UID={card_uid}）：{error}")
        return

    print(f"已綁定：UID={card_uid} -> 學號={student_id}")


def _handle_attendance(connection: sqlite3.Connection, card_uid: str, card_row: sqlite3.Row) -> None:
    """Running the attendance flow for a UID that already has a binding:
    appending one attendance record for the resolved student.
    """
    checked_at = datetime.now().isoformat(timespec="seconds")

    try:
        db.record_attendance(connection, card_uid, card_row["student_id"], checked_at)
    except sqlite3.Error as error:
        print(f"寫入出席紀錄失敗（UID={card_uid}）：{error}")
        return

    display_name = card_row["student_name"] or card_row["student_id"]
    print(f"已點名：{display_name}（學號={card_row['student_id']}） at {checked_at}")


def run() -> None:
    """Opening the database and serial port, then processing UID packets
    forever until interrupted.
    """
    connection = db.get_connection(config.DB_PATH)
    db.init_db(connection)

    try:
        for card_uid in read_card_uids(
            config.SERIAL_PORT,
            config.BAUD_RATE,
            config.SERIAL_TIMEOUT_SECONDS,
            config.RECONNECT_DELAY_SECONDS,
        ):
            card_row = db.find_card(connection, card_uid)
            if card_row is None:
                _handle_registration(connection, card_uid)
            else:
                _handle_attendance(connection, card_uid, card_row)
    finally:
        connection.close()
