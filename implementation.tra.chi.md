本專案旨在建置一套適用於教室場景的「無線獨立點名終端機」。系統透過 STM32 讀取 RFID/NFC 卡片卡號，經由蜂鳴器給予聲光回饋，並透過 HC-05 藍芽模組將卡號無線傳輸至教室電腦的虛擬 COM 埠，最後由電腦端 Python 腳本即時紀錄至資料庫（SQLite/MySQL）。

---

## 系統架構圖 (System Architecture)

```
[學生刷卡] ➔ [RC522 讀卡模組]
│ (SPI1)
▼
[STM32F767ZI 主控] ──(TIM2 PWM)──> [無源蜂鳴器 (叮咚聲)]
│ (USART6, Zio 排針 D1/D0)
▼
[HC-05 藍芽模組]
│
└─── (Bluetooth 無線串口傳輸) ───┐
                                ▼
[筆電 / 電腦 (資料庫)]
(自動識別為 Virtual COM Port)
```
---

## 一、 硬體與備料清單 (BOM)

| 類別 | 項目 | 規格 / 型號 | 數量 | 主要用途與備註 |
| :--- | :--- | :--- | :---: | :--- |
| **主控板** | 開發板 | **STM32 Nucleo-F767ZI** | 1 | 主控核心，負責讀卡、蜂鳴器控制與 UART 藍芽傳送 |
| **感應模組** | RFID 讀卡器 | **RC522** (SPI 介面) | 1 | 讀取學生證/悠遊卡/一卡通之 UID (工作電壓 3.3V) |
| **傳輸模組** | 藍芽模組 | **HC-05** | 1 | UART 串口藍芽從機，無線傳送資料至電腦 |
| **提示音效** | 發聲元件 | **無源蜂鳴器 (Passive Buzzer)** | 1 | 搭配 PWM 可產生溫和「叮咚～」刷卡提示音 |
| **線材與供電**| 線材與電源 | 杜邦線 (母對母/公對母) + 行動電源 | 1 組 | 提供點名機移動性，無需連接固定電源插座 |
| **接收端** | 電腦 / 筆電 | 具備藍芽功能之 PC | 1 | 執行 Python 腳本與建立 SQLite / MySQL 資料庫 |

---

## 二、 硬體腳位對照表 (Pinout)

> ⚠️ **重要注意事項**：
> 1. **RC522 只能接 3.3V**，誤接 5V 將會導致模組燒毀。
> 2. **UART TX/RX 必須交叉連接**（STM32 的 TX 接模組的 RX，反之亦然）。

### 1. RC522 RFID 模組 (SPI1)
| RC522 腳位 | STM32 Nucleo-F767ZI 引腳 | STM32 腳位功能 | 備註 |
| :--- | :--- | :--- | :--- |
| **VCC** | **3.3V** | Power (3.3V) | 絕對不可接 5V |
| **GND** | **GND** | Ground | 共地 |
| **RST** | **PA4** | GPIO_Output | 軟體重置腳 |
| **SDA (CS)**| **PB6** | GPIO_Output | SPI 片選腳 (Chip Select) |
| **SCK** | **PA5** | `SPI1_SCK` | SPI 時脈線 |
| **MISO** | **PA6** | `SPI1_MISO` | SPI 主機輸入 |
| **MOSI** | **PA7** | `SPI1_MOSI` | SPI 主機輸出 |

### 2. HC-05 藍芽模組 (USART6)
> ⚠️ **腳位選用說明**：Nucleo-144 系列（含 F767ZI）出廠預設將 **PD8/PD9（USART3）內部直接接到板載 ST-LINK**，用來做除錯用的虛擬 COM Port，**並未接到 Morpho 排針**（除非額外改焊板上的 Solder Bridge SB4/SB5/SB6/SB7）。若沿用 PD8/PD9 接藍芽模組，預設狀態下收不到資料。因此改用 **USART6（對應 Zio/Arduino 相容排針上標示的 D1/D0）**，此組腳位出廠即已接到排針、與 ST-LINK 除錯功能互不衝突，不需要動任何焊點。

| HC-05 腳位 | STM32 Nucleo-F767ZI 引腳 | STM32 腳位功能 | 備註 |
| :--- | :--- | :--- | :--- |
| **VCC** | **5V** (或 3.3V) | Power | 帶有 3.3V 穩壓 IC |
| **GND** | **GND** | Ground | 共地 |
| **TXD** | **PG9** (排針標示 `D0`) | `USART6_RX` | **交叉連接** (模組 TX $\rightarrow$ STM32 RX) |
| **RXD** | **PG14** (排針標示 `D1`) | `USART6_TX` | **交叉連接** (模組 RX $\rightarrow$ STM32 TX) |

### 3. 無源蜂鳴器 (TIM2_CH1)
| 蜂鳴器腳位 | STM32 Nucleo-F767ZI 引腳 | STM32 腳位功能 | 備註 |
| :--- | :--- | :--- | :--- |
| **VCC** | **3.3V** | Power | 電源輸入 |
| **GND** | **GND** | Ground | 共地 |
| **I/O (S)**| **PA15** | `TIM2_CH1` | PWM 輸出腳 (控制頻率與音節) |

---

## 三、 STM32CubeMX 配置指南

### 1. SPI1 設定 (RC522)
* **Mode**: Full-Duplex Master
* **Frame Format**: Motorola
* **Data Size**: 8 Bits
* **First Bit**: MSB First
* **Prescaler**: 調整至 SPI Clock 落在 **4 MHz ~ 8 MHz**。

### 2. GPIO 設定 (RC522 控制腳)
* **PB6** $\rightarrow$ 設為 `GPIO_Output` (標籤：`RC522_CS`)，預設 Level 設為 **High**。
* **PA4** $\rightarrow$ 設為 `GPIO_Output` (標籤：`RC522_RST`)，預設 Level 設為 **High**。

### 3. USART6 設定 (HC-05 藍芽)
* **Mode**: Asynchronous
* **GPIO**: `PG9` (`USART6_RX`) / `PG14` (`USART6_TX`)，即 Zio 排針上標示 `D0` / `D1`
* **Baud Rate**: **9600** (HC-05 預設通訊速率)
* **Word Length**: 8 Bits
* **Parity**: None
* **Stop Bits**: 1

### 4. TIM2 PWM 設定 (無源蜂鳴器)
* **Clock Source**: Internal Clock
* **Channel 1**: PWM Generation CH1 (`PA15`)
* **Prescaler (PSC)**: **`15`**
  * *說明*：在未啟用 PLL 情況下，系統採用內部 HSI 時脈 (16 MHz)。$\text{PSC}=15$ 使得定時器計數頻率降為 $\frac{16\,\text{MHz}}{15+1} = 1\,\text{MHz}$（即每秒計數 $1,000,000$ 次），後續音頻計算公式得以精確成立。
* **Counter Period (ARR)**: `1000` (後續可在程式碼中動態修改 ARR 以切換音調)

---

## 四、 核心程式碼範例 (C Language)

### 1. 藍芽發送刷卡資料 (UART Output)
```c
#include <stdio.h>
#include <string.h>

// 當 RC522 讀取到卡號 uid (4 bytes) 時呼叫此函式
void Send_Card_ID_Via_Bluetooth(uint8_t *uid) {
    char bt_buffer[64];
    
    // 格式化卡號字串
    snprintf(bt_buffer, sizeof(bt_buffer), "CARD:%02X%02X%02X%02X\r\n", 
             uid[0], uid[1], uid[2], uid[3]);

    // 經由 USART6 透過 HC-05 無線傳送給電腦
    HAL_UART_Transmit(&huart6, (uint8_t*)bt_buffer, strlen(bt_buffer), 100);
}
```
2. 無源蜂鳴器「叮咚～」音效控制 (PWM Output)
```C
void Play_DingDong(void) {
    // 註：定時器計數頻率已被 PSC=15 設定為 1 MHz (1,000,000 Hz)
    
    // 「叮」 (高音 C6 ≒ 1046 Hz)
    __HAL_TIM_SET_AUTORELOAD(&htim2, 1000000 / 1046);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (1000000 / 1046) / 2); // 50% 占空比
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_Delay(100);

    // 「咚」 (低音 G5 ≒ 784 Hz)
    __HAL_TIM_SET_AUTORELOAD(&htim2, 1000000 / 784);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (1000000 / 784) / 2);
    HAL_Delay(200);

    // 關閉聲音
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
}
```
五、 電腦端部署與整合 (Host PC Setup)
配對藍芽：

開啟電腦藍芽，搜尋並配對 HC-05 模組（預設配對 PIN 碼為 1234 或 0000）。

配對完成後，裝置管理員會生成一個 Virtual COM Port (例如：COM3 或 /dev/rfcomm0)。

Python 監聽與寫庫：

使用 pyserial 監聽該 Virtual COM Port。

解析 CARD:XXXXXX 卡號格式，比對系統目前時間，自動寫入 SQLite / MySQL 資料庫。

（Host 端詳細建置、環境設定、執行與疑難排解步驟，見以下第六節。）

---

## 六、 Host 端（`Host/`）建置與執行說明

規劃日期：2026-08-23
對應範圍：`Host/`（`rollcall_host` 套件 + `main.py`），對接 MCU 端 `Send_Card_ID_Via_Bluetooth()` 送出的 `CARD:XXXXXX` 封包（見本文件第四節、`RC522Test.md` 第 9 節）。

### 6.0 與其他文件的關係

- 本文件前半部（一～四節）：整體架構說明，含 HC-05 藍芽模組與 UART 協定格式。
- `RC522Test.md` 第 9 節：UID ↔ 學號對照表的「方案一」決策——MCU 端不判斷註冊/點名，全部邏輯放在本節說明的 Python 端。
- `SA.md` 第 4 節：`cards`/`attendance` 資料表欄位設計的原始分析，`Host/rollcall_host/db.py` 是其實作。
- 本節（原 `host.md`，現已併入本文件）：只涵蓋 **Host 端如何裝、如何連線、如何跑**，不重複描述 MCU 端接線（見 `RC522Test.md`）。

---

### 6.1 Host 端在整個系統中的角色

```
[HC-05 藍芽模組] --(藍芽序列埠)--> [Windows 虛擬 COM Port] --(pyserial)--> [Host/main.py]
                                                                              │
                                                                    解析 CARD:XXXXXX
                                                                              │
                                                                    查 SQLite 對照表
                                                                    ┌─────────┴─────────┐
                                                              查無此 UID           查到此 UID
                                                              → 進註冊流程          → 寫出席紀錄
```

- `serial_listener.py`：開啟 COM Port，逐行讀取，只把符合 `CARD:XXXXXX` 格式的行 yield 出去；連線失敗會自動重試，不會讓程式崩潰。
- `db.py`：SQLite 的 schema 與存取函式（`cards`、`attendance` 兩張表）。
- `cli.py`：主迴圈，收到 UID 後查表，決定走註冊流程還是點名流程。
- `config.py`：所有環境相關設定（COM Port、Baud Rate、DB 路徑等），從 `.env` 讀取，見第 6.4 節。

---

### 6.2 前置作業：確認藍芽鏈路本身可用

**在裝 Python 環境之前，先確認硬體/配對沒問題**，避免除錯時分不清是程式問題還是硬體問題：

1. Windows 設定 → 藍牙與裝置，搜尋並配對 HC-05（PIN 通常 `1234` 或 `0000`）。
2. 控制台 → 裝置和印表機，右鍵該藍牙裝置 → 內容 → **服務（Services）** 分頁，或搜尋「藍牙 COM 連接埠」，記下 **「輸出（Outgoing）」** 的 COM 編號（例如 `COM6`）——Host 端要接的是這個，不是 Incoming。
3. （可選）先用 Termite/PuTTY 開那個虛擬 COM，9600 8N1，確認開發板重置後真的能收到 `RC522 Init OK` 等字串，排除藍芽鏈路本身的問題。詳細接線與測試 SOP 見 `RC522Test.md` 第 5～7 節。

---

### 6.3 Python 環境安裝

#### 6.3.1 選對 Shell / Python（尤其是 MSYS2 使用者）

MSYS2 不是獨立系統，藍芽的 COM Port 是 Windows 層級的裝置，任何跑在這台 Windows 上的程式都能開——但**用哪個 Python 執行**會影響能不能正常抓到序列埠：

| 環境 | 說明 | 建議 |
|---|---|---|
| MSYS2「MSYS」基底環境（`pacman -S python`） | 走 POSIX 相容層，`sys.platform` 不是 `win32`，pyserial 容易抓不到高編號的虛擬 COM Port | ❌ 不建議 |
| MSYS2「MINGW64」/「UCRT64」 | 原生 Win32 執行檔，序列埠存取正常 | ✅ 建議 |
| 系統原生 Windows Python（python.org 安裝，PATH 上找得到，從任何 shell 呼叫都行） | 原生 Win32 執行檔 | ✅ 最簡單 |

#### 6.3.2 建立虛擬環境（venv）

MSYS2 的 Python 由 `pacman` 管理，屬於「externally-managed」環境，直接 `pip install` 常會被擋下（`error: externally-managed-environment`）。統一用 venv 是最乾淨、不會踩雷的做法：

```bash
cd Host
python -m venv .venv              # 建立虛擬環境
source .venv/Scripts/activate     # 啟用（MSYS2/Git Bash 語法，注意是 Scripts 不是 bin）
python -m pip install -r requirements.txt
```

- 啟用成功後，shell 提示字元前面會出現 `(.venv)`。
- 之後每次開新終端機，只要在 `Host/` 目錄下重新 `source .venv/Scripts/activate` 即可，不用重灌。
- CMD／PowerShell 使用者對應改用 `.venv\Scripts\activate.bat` 或 `.venv\Scripts\Activate.ps1`。

`Host/requirements.txt` 內容：

```
pyserial>=3.5
python-dotenv>=1.0
```

`pip install -r requirements.txt` 就是依這份清單把兩個套件一次裝齊：`pyserial` 給 `serial_listener.py` 存取序列埠，`python-dotenv` 給 `config.py` 讀 `.env`。

---

### 6.4 環境設定（`.env`）

機器相關的設定（COM Port 最典型）不寫死在 `config.py`，而是放在**不進版控**的 `.env` 檔，換機器或重新配對只改這一份即可：

```bash
copy .env.example .env
```

`Host/.env.example`（已加進 git，作為範本）：

```
SERIAL_PORT=COM3
BAUD_RATE=9600
SERIAL_TIMEOUT_SECONDS=1.0
RECONNECT_DELAY_SECONDS=3.0
DB_PATH=rollcall.db
```

編輯 `.env`，把 `SERIAL_PORT` 改成第 6.2 節量到的實際 COM 編號。其餘四個值通常不用動：`BAUD_RATE` 要與 MCU 端 `USART3` 設定一致（見 `RollCall_RC522.ioc` 的 `USART3.BaudRate=9600`），其他兩個是逾時/重試秒數與 DB 檔案路徑。

`config.py` 讀取邏輯：`.env` 有設就用 `.env` 的值，沒設就退回程式內建的預設值——所以就算沒建立 `.env`，程式仍可用預設值執行。

---

### 6.5 SQLite：不用額外安裝

- `sqlite3` 是 Python 標準函式庫內建模組，只要有裝 Python 就有，不用 `pip install`（`requirements.txt` 裡沒有它）。
- SQLite 沒有 client-server 架構，就是一個單一檔案，`db.get_connection(db_path)`（`db.py:13-21`）呼叫 `sqlite3.connect(db_path)`：
  - 檔案不存在 → 自動建立空白 `.db` 檔
  - 檔案已存在 → 直接開啟
- `db_path` 對應 `.env` 的 `DB_PATH`，預設 `rollcall.db`，會建在執行 `python main.py` 當下的工作目錄。
- 第一次執行後，可用 `DB Browser for SQLite` 或 `sqlite3` CLI 打開 `rollcall.db` 檢視 `cards`／`attendance` 兩張表的內容。

---

### 6.6 執行

```bash
cd Host
source .venv/Scripts/activate     # 若尚未啟用
python main.py
```

看到終端機開始印連線訊息（`Connected to COM6 at 9600 baud.`）代表序列埠開啟成功。之後每次收到合法的 `CARD:XXXXXX` 封包：

- **查無此 UID** → 進註冊流程，提示輸入學號（可選姓名），寫入 `cards` 表。
- **查到此 UID** → 直接寫一筆 `attendance` 出席紀錄，印出對應學生姓名/學號。

`Ctrl+C` 可安全中斷（`main.py` 有攔截 `KeyboardInterrupt`）。

---

### 6.7 疑難排解對照表

| 現象 | 可能原因 |
|---|---|
| `pip install -r requirements.txt` 出現 `externally-managed-environment` | 沒用 venv，直接對系統/MSYS2 的 Python 裝套件；建立並啟用 `.venv` 後重試（見第 6.3.2 節） |
| `python -m serial.tools.list_ports -v` 列不出藍芽 COM Port | 用錯 Python（例如 MSYS2 基底環境而非 MINGW64/UCRT64）；或裝置尚未配對成功 |
| `Serial error on COMx: ...`，程式一直重試 | `.env` 裡 `SERIAL_PORT` 打錯編號、HC-05 未上電、或該 COM Port 被其他程式（如終端機軟體）占用中 |
| 連線正常但完全沒有任何 `CARD:` 訊息 | MCU 端韌體目前送出的是除錯格式 `UID(%u): ...`（見 `RC522Test.md` 第 5 節），尚未接上正式的 `Send_Card_ID_Via_Bluetooth()`／`CARD:XXXXXX` 協定 |
| `Ignoring malformed line: ...` | 收到的內容不符合 `CARD:XXXXXX` 格式，通常是還在跑除錯格式，或藍芽訊號雜訊造成半包 |
| 每次都跳「未登記卡片」，即使是同一張卡 | `rollcall.db` 可能被刪除或換了工作目錄（`DB_PATH` 是相對路徑），導致每次都是全新資料庫 |

---

### 6.8 待辦事項

- [ ] MCU 端把除錯用的 `UID(%u): ...` 輸出格式，換成正式的 `Send_Card_ID_Via_Bluetooth()`／`CARD:XXXXXX` 協定（見 `RC522Test.md` 第 8～9 節）。
- [ ] Host 端目前用 `print()` 輸出訊息，之後若要長期運行，可考慮換成 `logging` 並寫檔，方便事後追查漏刷/異常。
- [ ] 「輸入學號」目前是 CLI `input()` 阻塞式提示；若要在教室連續刷卡場景下使用，評估是否需要改成非阻塞或簡易 GUI。