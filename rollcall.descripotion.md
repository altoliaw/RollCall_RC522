# STM32 藍芽 RFID 無線點名機專案 (Bluetooth RFID Roll Call Terminal)

本專案旨在建置一套適用於教室場景的「無線獨立點名終端機」。系統透過 STM32 讀取 RFID/NFC 卡片卡號，經由蜂鳴器給予聲光回饋，並透過 HC-06 藍芽模組將卡號無線傳輸至教室電腦的虛擬 COM 埠，最後由電腦端 Python 腳本即時紀錄至資料庫（SQLite/MySQL）。

---
## 系統架構圖 (System Architecture)
```
[學生刷卡] ➔ [RC522 讀卡模組]
│ (SPI1)
▼
[STM32F767ZI 主控] ──(TIM2 PWM)──> [無源蜂鳴器 (叮咚聲)]
│ (USART3)
▼
[HC-06 藍芽模組]
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
| **傳輸模組** | 藍芽模組 | **HC-06** (或 HC-05) | 1 | UART 串口藍芽從機，無線傳送資料至電腦 |
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

### 2. HC-06 藍芽模組 (USART3)
| HC-06 腳位 | STM32 Nucleo-F767ZI 引腳 | STM32 腳位功能 | 備註 |
| :--- | :--- | :--- | :--- |
| **VCC** | **5V** (或 3.3V) | Power | 帶有 3.3V 穩壓 IC |
| **GND** | **GND** | Ground | 共地 |
| **TXD** | **PD9** | `USART3_RX` | **交叉連接** (模組 TX $\rightarrow$ STM32 RX) |
| **RXD** | **PD8** | `USART3_TX` | **交叉連接** (模組 RX $\rightarrow$ STM32 TX) |

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

### 3. USART3 設定 (HC-06 藍芽)
* **Mode**: Asynchronous
* **Baud Rate**: **9600** (HC-06 預設通訊速率)
* **Word Length**: 8 Bits
* **Parity**: None
* **Stop Bits**: 1

### 4. TIM2 PWM 設定 (無源蜂鳴器)
* **Clock Source**: Internal Clock
* **Channel 1**: PWM Generation CH1 (`PA15`)
* **Prescaler (PSC)**: `83` (若定時器時脈為 84MHz，將計數頻率除降為 1 MHz)
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

    // 經由 USART3 透過 HC-06 無線傳送給電腦
    HAL_UART_Transmit(&huart3, (uint8_t*)bt_buffer, strlen(bt_buffer), 100);
}
```

2. 無源蜂鳴器「叮咚～」音效控制 (PWM Output)
```C
void Play_DingDong(void) {
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

開啟電腦藍芽，搜尋並配對 HC-06 模組（預設配對 PIN 碼為 1234 或 0000）。

配對完成後，裝置管理員會生成一個 Virtual COM Port (例如：COM3 或 /dev/rfcomm0)。

Python 監聽與寫庫：

使用 pyserial 監聽該 Virtual COM Port。

解析 CARD:XXXXXX 卡號格式，比對系統目前時間，自動寫入 SQLite / MySQL 資料庫。