This project establishes a wireless, stand-alone roll-call terminal intended for classroom use. The system employs an STM32 microcontroller to read the unique identifier (UID) of an RFID/NFC card, provides audible feedback through a buzzer, and transmits the UID wirelessly to the classroom computer's virtual COM port via an HC-05 Bluetooth module. A Python script running on the host computer then records each transmission to a database (SQLite or MySQL) in real time.

---

## System Architecture

```
[Student taps card] -> [RC522 reader module]
│ (SPI1)
▼
[STM32F767ZI MCU] --(TIM2 PWM)--> [Passive buzzer (chime tone)]
│ (USART6, Zio header D1/D0)
▼
[HC-05 Bluetooth module]
│
└─── (Bluetooth wireless serial link) ───┐
                                          ▼
[Laptop / PC (database)]
(automatically enumerated as a virtual COM port)
```
---

## I. Hardware and Bill of Materials (BOM)

| Category | Item | Specification / Model | Qty | Purpose and Notes |
| :--- | :--- | :--- | :---: | :--- |
| **Main controller** | Development board | **STM32 Nucleo-F767ZI** | 1 | Serves as the core controller, handling card reading, buzzer control, and UART-based Bluetooth transmission. |
| **Sensing module** | RFID reader | **RC522** (SPI interface) | 1 | Reads the UID of a student ID card, transit card, or similar contactless card (operating voltage: 3.3 V). |
| **Transmission module** | Bluetooth module | **HC-05** | 1 | Functions as a UART serial Bluetooth slave, transmitting data wirelessly to the host computer. |
| **Audible feedback** | Sound-producing element | **Passive buzzer** | 1 | Driven by PWM to produce a gentle "ding-dong" tap confirmation tone. |
| **Cabling and power** | Cables and power supply | Jumper wires (F-F / M-F) + power bank | 1 set | Provides the terminal with portability, eliminating the need for a fixed power outlet. |
| **Receiving end** | Computer / laptop | Bluetooth-capable PC | 1 | Runs the Python script and hosts the SQLite / MySQL database. |

---

## II. Hardware Pinout

> ⚠️ **Important notices**:
> 1. **The RC522 module accepts only 3.3 V.** Connecting it to 5 V will permanently damage the module.
> 2. **The UART TX/RX lines must be cross-connected** — that is, the STM32's TX pin must connect to the module's RX pin, and vice versa.

### 1. RC522 RFID Module (SPI1)
| RC522 Pin | STM32 Nucleo-F767ZI Pin | STM32 Pin Function | Notes |
| :--- | :--- | :--- | :--- |
| **VCC** | **3.3 V** | Power (3.3 V) | Must never be connected to 5 V. |
| **GND** | **GND** | Ground | Common ground. |
| **RST** | **PA4** | GPIO_Output | Software reset line. |
| **SDA (CS)**| **PB6** | GPIO_Output | SPI chip-select line. |
| **SCK** | **PA5** | `SPI1_SCK` | SPI clock line. |
| **MISO** | **PA6** | `SPI1_MISO` | SPI master-input line. |
| **MOSI** | **PA7** | `SPI1_MOSI` | SPI master-output line. |

### 2. HC-05 Bluetooth Module (USART6)
> ⚠️ **Note on pin selection**: On the Nucleo-144 series (including the F767ZI), pins PD8/PD9 (USART3) are, by factory default, wired internally to the on-board ST-LINK for virtual-COM-port debugging and are **not routed to the Morpho headers** unless the solder bridges SB4/SB5/SB6/SB7 are manually reworked. Consequently, connecting the Bluetooth module to PD8/PD9 will not receive any data under the default configuration. This design therefore uses **USART6**, which corresponds to pins **D1/D0 on the Zio/Arduino-compatible header**. These pins are routed to the header at the factory, do not conflict with the ST-LINK debug function, and require no rework of solder bridges.

| HC-05 Pin | STM32 Nucleo-F767ZI Pin | STM32 Pin Function | Notes |
| :--- | :--- | :--- | :--- |
| **VCC** | **5 V** (or 3.3 V) | Power | The module includes an on-board 3.3 V regulator. |
| **GND** | **GND** | Ground | Common ground. |
| **TXD** | **PG9** (labeled `D0`) | `USART6_RX` | **Cross-connected**: module TX → STM32 RX. |
| **RXD** | **PG14** (labeled `D1`) | `USART6_TX` | **Cross-connected**: module RX → STM32 TX. |

### 3. Passive Buzzer (TIM2_CH1)
| Buzzer Pin | STM32 Nucleo-F767ZI Pin | STM32 Pin Function | Notes |
| :--- | :--- | :--- | :--- |
| **VCC** | **3.3 V** | Power | Power input. |
| **GND** | **GND** | Ground | Common ground. |
| **I/O (S)**| **PA15** | `TIM2_CH1` | PWM output, controlling frequency and note duration. |

---

## III. STM32CubeMX Configuration Guide

### 1. SPI1 Configuration (RC522)
* **Mode**: Full-Duplex Master.
* **Frame Format**: Motorola.
* **Data Size**: 8 bits.
* **First Bit**: MSB first.
* **Prescaler**: Set such that the resulting SPI clock falls within **4 MHz to 8 MHz**.

### 2. GPIO Configuration (RC522 Control Pins)
* **PB6** is configured as `GPIO_Output` (labeled `RC522_CS`), with its default level set to **High**.
* **PA4** is configured as `GPIO_Output` (labeled `RC522_RST`), with its default level set to **High**.

### 3. USART6 Configuration (HC-05 Bluetooth)
* **Mode**: Asynchronous.
* **GPIO**: `PG9` (`USART6_RX`) and `PG14` (`USART6_TX`), corresponding to pins `D0`/`D1` on the Zio header.
* **Baud Rate**: **9600** (the HC-05 default communication rate).
* **Word Length**: 8 bits.
* **Parity**: None.
* **Stop Bits**: 1.

### 4. TIM2 PWM Configuration (Passive Buzzer)
* **Clock Source**: Internal clock.
* **Channel 1**: PWM generation on CH1 (`PA15`).
* **Prescaler (PSC)**: **`15`**.
  * *Rationale*: With the PLL disabled, the system clock is driven by the internal HSI oscillator (16 MHz). Setting $\text{PSC}=15$ reduces the timer's counting frequency to $\frac{16\,\text{MHz}}{15+1} = 1\,\text{MHz}$ (that is, $1,000,000$ counts per second), which allows the subsequent tone-frequency calculations to hold exactly.
* **Counter Period (ARR)**: `1000` (this value may subsequently be modified at run time to change the emitted pitch).

---

## IV. Core Code Examples (C Language)

### 1. Transmitting Card Data over Bluetooth (UART Output)
```c
#include <stdio.h>
#include <string.h>

// Sends the four-byte UID to the host computer after the RC522 reads a card.
void Send_Card_ID_Via_Bluetooth(uint8_t *uid) {
    char bt_buffer[64];

    // Formats the UID as a "CARD:XXXXXXXX" ASCII string.
    snprintf(bt_buffer, sizeof(bt_buffer), "CARD:%02X%02X%02X%02X\r\n",
             uid[0], uid[1], uid[2], uid[3]);

    // Transmits the formatted string to the host computer over USART6 via the HC-05 module.
    HAL_UART_Transmit(&huart6, (uint8_t*)bt_buffer, strlen(bt_buffer), 100);
}
```

### 2. Passive-Buzzer "Ding-Dong" Tone Control (PWM Output)
```c
// Plays a two-note "ding-dong" confirmation tone on the passive buzzer.
void Play_DingDong(void) {
    // Note: PSC=15 configures the timer's counting frequency to 1 MHz (1,000,000 Hz).

    // Plays the "ding" note (high pitch, C6, approximately 1046 Hz).
    __HAL_TIM_SET_AUTORELOAD(&htim2, 1000000 / 1046);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (1000000 / 1046) / 2); // 50% duty cycle
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_Delay(100);

    // Plays the "dong" note (low pitch, G5, approximately 784 Hz).
    __HAL_TIM_SET_AUTORELOAD(&htim2, 1000000 / 784);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (1000000 / 784) / 2);
    HAL_Delay(200);

    // Stops PWM output, silencing the buzzer.
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
}
```

## V. Host PC Deployment and Integration

**Bluetooth pairing.** Enable Bluetooth on the host computer, then discover and pair the HC-05 module (the default pairing PIN is typically `1234` or `0000`). Once pairing completes, Device Manager creates a virtual COM port (for example, `COM3` on Windows or `/dev/rfcomm0` on Linux).

**Python listener and database write-back.** A `pyserial`-based listener monitors the virtual COM port, parses incoming data against the `CARD:XXXXXXXX` format, timestamps each reading against the system clock, and writes the resulting record into the SQLite or MySQL database automatically.

(Detailed Host-side setup, environment configuration, execution, and troubleshooting procedures are provided in Section VI below.)

---

## VI. Host-Side (`Host/`) Setup and Execution Guide

Planning date: 2026-08-23.
Scope: `Host/` (the `rollcall_host` package and `main.py`), which consumes the `CARD:XXXXXXXX` packets emitted by the MCU-side `Send_Card_ID_Via_Bluetooth()` function (see Section IV of this document and Section 9 of `RC522Test.md`).

### 6.0 Relationship to Other Documents

- The first half of this document (Sections I–IV) describes the overall system architecture, including the HC-05 Bluetooth module and the UART protocol format.
- Section 9 of `RC522Test.md` documents the "Option 1" decision for the UID-to-student-number mapping: the MCU side performs no registration/attendance logic of its own, and all such logic resides in the Python code described in this section.
- Section 4 of `SA.md` contains the original analysis behind the `cards` and `attendance` table schemas, which `Host/rollcall_host/db.py` implements.
- This section (originally `host.md`, now merged into this document) covers **only how to install, connect, and run the Host side**; it does not repeat the MCU-side wiring instructions, which are documented in `RC522Test.md`.

---

### 6.1 The Host's Role within the Overall System

```
[HC-05 Bluetooth module] --(Bluetooth serial link)--> [Windows virtual COM port] --(pyserial)--> [Host/main.py]
                                                                                                        │
                                                                                            Parses CARD:XXXXXXXX
                                                                                                        │
                                                                                            Looks up the SQLite table
                                                                                            ┌───────────┴───────────┐
                                                                                       UID not found          UID found
                                                                                       → registration flow    → attendance record written
```

- `serial_listener.py` opens the COM port, reads it line by line, and yields only lines matching the `CARD:XXXXXXXX` format; on connection failure it retries automatically rather than crashing the program.
- `db.py` defines the SQLite schema and the accessor functions for the `cards` and `attendance` tables.
- `cli.py` implements the main loop: upon receiving a UID, it queries the table and dispatches either the registration flow or the attendance flow.
- `config.py` centralizes all environment-dependent settings (COM port, baud rate, database path, and so on), read from `.env` as described in Section 6.4.

---

### 6.2 Prerequisite: Verifying the Bluetooth Link Independently

**Before installing the Python environment, first confirm that the hardware and pairing are functioning correctly.** Doing so avoids ambiguity, during later debugging, between a software problem and a hardware problem.

1. In Windows Settings, navigate to Bluetooth & devices, then discover and pair the HC-05 module (the PIN is typically `1234` or `0000`).
2. In Control Panel, open Devices and Printers, right-click the paired Bluetooth device, select Properties, and open the **Services** tab (alternatively, search for "Bluetooth SPP COM port"). Record the COM port number assigned to the **"Outgoing"** direction (for example, `COM6`) — this is the port the Host side must open; the "Incoming" port is not used.
3. (Optional) Open the identified virtual COM port with a terminal program such as Termite or PuTTY at 9600 8N1, and confirm that resetting the development board actually produces output such as `RC522 Init OK`. This step isolates problems in the Bluetooth link itself. The detailed wiring and test procedure are given in Sections 5–7 of `RC522Test.md`.

---

### 6.3 Python Environment Setup

#### 6.3.1 Choosing the Correct Shell and Python Interpreter (Particularly for MSYS2 Users)

MSYS2 is not a separate operating system; the Bluetooth COM port is a Windows-level device, and any program running on this Windows installation can open it. However, **which Python interpreter is used to run the script** does affect whether the serial port can be detected correctly:

| Environment | Description | Recommendation |
|---|---|---|
| MSYS2 "MSYS" base environment (`pacman -S python`) | Runs through a POSIX compatibility layer; `sys.platform` is not reported as `win32`, and pyserial frequently fails to detect high-numbered virtual COM ports. | ❌ Not recommended |
| MSYS2 "MINGW64" / "UCRT64" | A native Win32 executable; serial-port access functions correctly. | ✅ Recommended |
| System-native Windows Python (installed from python.org, discoverable on `PATH`, callable from any shell) | A native Win32 executable. | ✅ Simplest option |

#### 6.3.2 Creating a Virtual Environment (venv)

The Python interpreter that MSYS2 manages through `pacman` is an "externally managed" environment, and a direct `pip install` is frequently blocked with `error: externally-managed-environment`. Using a venv consistently is the cleanest approach and avoids this pitfall entirely:

```bash
cd Host
python -m venv .venv              # Creates the virtual environment.
source .venv/Scripts/activate     # Activates it (MSYS2/Git Bash syntax; note the directory is Scripts, not bin).
python -m pip install -r requirements.txt
```

- Once activation succeeds, the shell prompt is prefixed with `(.venv)`.
- In each subsequent terminal session, running `source .venv/Scripts/activate` again from within `Host/` is sufficient; reinstallation is not required.
- CMD and PowerShell users should instead run `.venv\Scripts\activate.bat` or `.venv\Scripts\Activate.ps1`, respectively.

Contents of `Host/requirements.txt`:

```
pyserial>=3.5
python-dotenv>=1.0
```

Running `pip install -r requirements.txt` installs both packages listed above in a single step: `pyserial` provides `serial_listener.py` with serial-port access, and `python-dotenv` provides `config.py` with the ability to read `.env`.

---

### 6.4 Environment Configuration (`.env`)

Machine-specific settings — the COM port being the most common example — are not hard-coded into `config.py`. Instead, they are placed in an `.env` file that is excluded from version control, so that switching machines or re-pairing the Bluetooth device requires editing only this one file:

```bash
copy .env.example .env
```

Contents of `Host/.env.example` (checked into git as a template):

```
SERIAL_PORT=COM3
BAUD_RATE=9600
SERIAL_TIMEOUT_SECONDS=1.0
RECONNECT_DELAY_SECONDS=3.0
DB_PATH=rollcall.db
```

Edit `.env` and set `SERIAL_PORT` to the actual COM port number identified in Section 6.2. The remaining four values generally do not need to be changed: `BAUD_RATE` must match the MCU-side `USART3` setting (see `USART3.BaudRate=9600` in `RollCall_RC522.ioc`), while the other two values control the timeout/retry interval and the database file path, respectively.

Read logic in `config.py`: a value is taken from `.env` when present, and falls back to the built-in default otherwise — consequently, the program will still run using its default values even if `.env` has not been created.

---

### 6.5 SQLite: No Additional Installation Required

- `sqlite3` is part of the Python standard library and is available with any Python installation; it does not need to be installed via `pip` and is therefore absent from `requirements.txt`.
- SQLite has no client-server architecture and consists of a single file. `db.get_connection(db_path)` (`db.py:13-21`) calls `sqlite3.connect(db_path)`, which:
  - creates a new, empty `.db` file automatically if none exists, or
  - opens the existing file directly if one is already present.
- `db_path` corresponds to the `DB_PATH` value in `.env`, defaulting to `rollcall.db`, and is created in the working directory in effect when `python main.py` is executed.
- After the first run, the contents of the `cards` and `attendance` tables in `rollcall.db` may be inspected using DB Browser for SQLite or the `sqlite3` command-line tool.

---

### 6.6 Execution

```bash
cd Host
source .venv/Scripts/activate     # If not already activated.
python main.py
```

A connection message printed to the terminal (for example, `Connected to COM6 at 9600 baud.`) indicates that the serial port was opened successfully. Thereafter, for each valid `CARD:XXXXXXXX` packet received:

- **If the UID is not found**, the program enters the registration flow, prompting for a student number (and, optionally, a name), and inserts the result into the `cards` table.
- **If the UID is found**, the program writes an attendance record directly to the `attendance` table and prints the corresponding student name and number.

The program can be interrupted safely with `Ctrl+C`, since `main.py` catches `KeyboardInterrupt`.

---

### 6.7 Troubleshooting Reference

| Symptom | Probable Cause |
|---|---|
| `pip install -r requirements.txt` reports `externally-managed-environment`. | A venv was not used, and packages were installed directly into the system or MSYS2 Python. Create and activate a `.venv` and retry (see Section 6.3.2). |
| `python -m serial.tools.list_ports -v` does not list the Bluetooth COM port. | The wrong Python interpreter is in use (for example, the MSYS2 base environment rather than MINGW64/UCRT64), or the device has not yet been paired successfully. |
| `Serial error on COMx: ...` is reported repeatedly as the program retries. | The `SERIAL_PORT` value in `.env` is incorrect, the HC-05 module is unpowered, or the COM port is already held open by another program (such as a terminal application). |
| The connection succeeds, but no `CARD:` messages are ever received. | The MCU firmware is currently emitting the debug-format output `UID(%u): ...` (see Section 5 of `RC522Test.md`) rather than the production `Send_Card_ID_Via_Bluetooth()` / `CARD:XXXXXXXX` protocol. |
| `Ignoring malformed line: ...` is printed. | The received content does not match the `CARD:XXXXXXXX` format — typically because the firmware is still running in debug mode, or because Bluetooth noise has corrupted part of a packet. |
| The program reports "unregistered card" every time, even for the same physical card. | `rollcall.db` may have been deleted, or the working directory has changed (`DB_PATH` is a relative path), so that each run starts from a fresh, empty database. |

---

### 6.8 Outstanding Items

- [ ] Replace the MCU-side debug-format output `UID(%u): ...` with the production `Send_Card_ID_Via_Bluetooth()` / `CARD:XXXXXXXX` protocol (see Sections 8–9 of `RC522Test.md`).
- [ ] The Host side currently uses `print()` for all output; for sustained long-term operation, consider switching to the `logging` module with file output to simplify post-hoc investigation of missed or anomalous readings.
- [ ] The student-number prompt is currently a blocking CLI `input()` call; evaluate whether a non-blocking approach or a lightweight GUI is needed to support continuous card-tapping in a classroom setting.
