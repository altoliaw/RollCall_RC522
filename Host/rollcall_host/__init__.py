"""Exposing the roll-call host package, which listens on a virtual COM port for
`CARD:XXXXXX` packets sent by the STM32 firmware and turns them into either a
card-registration event or an attendance record, per the design in
`SA.md` and `RC522Test.md` (section 9) at the repository root.
"""
