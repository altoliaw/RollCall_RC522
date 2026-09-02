# RollCall_RC522

一套適用於教室場景的**無線獨立點名終端機**。STM32 讀取 RFID/NFC 卡片 UID，蜂鳴器提供聲音回饋，再透過 HC-05 藍芽模組把卡號無線傳給電腦，由 Host 端 Python 腳本即時寫入資料庫（SQLite）。

```
[學生刷卡] → [RC522] → [STM32F767ZI] → [蜂鳴器回饋]
                              │ (USART6 / HC-05 藍芽)
                              ▼
                    [藍芽虛擬序列埠] → [Python Host] → [SQLite 資料庫]
```

---

## 目錄結構

```
RollCall_RC522/
├── Core/                  # STM32CubeMX 產生的主程式與 HAL 設定 (main.c 等)
├── Drivers/               # STM32 HAL / CMSIS 函式庫
├── Modules/RC522/         # RC522 RFID 驅動 (rc522.c / rc522.h)
├── Vendors/               # 第三方/廠商程式碼
├── cmake/                 # CMake 工具鏈與 CubeMX 產生的子專案
├── Host/                  # 電腦端 Python 監聽與資料庫寫入程式 (rollcall_host)
├── RollCall_RC522.ioc     # STM32CubeMX 專案設定
├── CMakeLists.txt / CMakePresets.json
└── startup_stm32f767xx.s / STM32F767xx_FLASH.ld
```

---

## 開發環境

本專案在 **MSYS2 MINGW64** shell 下開發與建置。以下為目前本機驗證過可正常運作的版本組合（`pacman -Q` 查得的套件版本）：

| 工具 | 版本 | 來源 / 套件名稱 |
| :--- | :--- | :--- |
| MSYS2 (uname) | `3.6.3` | — |
| CMake | `4.3.2` | `mingw-w64-x86_64-cmake` |
| Ninja | `1.13.2` | `mingw-w64-x86_64-ninja` |
| GNU Make | `4.4.1` | （MSYS base，供參考；本專案主要用 Ninja） |
| arm-none-eabi-gcc | `13.3.0` | `mingw-w64-x86_64-arm-none-eabi-gcc` |
| arm-none-eabi-binutils | `2.46.0` | `mingw-w64-x86_64-arm-none-eabi-binutils` |
| arm-none-eabi-newlib | `4.5.0.20241231` | `mingw-w64-x86_64-arm-none-eabi-newlib` |
| OpenOCD | `0.12.0` | `mingw-w64-x86_64-openocd` |
| Git | `2.50.1` | — |
| Python (MSYS2 MINGW64) | `3.14.5` | `mingw-w64-x86_64-python` |
| ├─ pyserial | `3.5` | `mingw-w64-x86_64-python-pyserial` |
| └─ python-dotenv | `1.2.2` | `mingw-w64-x86_64-python-dotenv` |
| STM32CubeMX | `6.18.1` | 見 `RollCall_RC522.ioc` 的 `MxCube.Version` |
| STM32Cube FW_F7 | `V1.17.4` | 見 `RollCall_RC522.ioc` 的 `ProjectManager.FirmwarePackage` |
| 目標晶片 | STM32F767ZIT6 (Cortex-M7) | Nucleo-F767ZI |

⚠️ **注意事項**：
- 建議固定使用同一套 Python 直譯器並搭配 venv，避免序列埠偵測異常。
- 目前 `Host/` 尚未建立 `.venv`；`requirements.txt`（`pyserial>=3.5`、`python-dotenv>=1.0`）與 MSYS2 全域已安裝的版本相容，但仍建議建立虛擬環境以求乾淨隔離。
- `st-info`／ST-LINK 工具未偵測到已安裝，若需用 ST-LINK 燒錄／除錯，需另外安裝 STM32CubeProgrammer 或 `stlink` 工具組。
- `clangd` 未偵測到已安裝；`CMakeLists.txt` 已啟用 `CMAKE_EXPORT_COMPILE_COMMANDS`，若要接上 clangd 做編輯器索引，需另外安裝。

---

## 建置韌體 (MCU Firmware)

使用 CMake Presets + Ninja + `arm-none-eabi-gcc` 工具鏈（見 `cmake/gcc-arm-none-eabi.cmake`）：

```bash
# 於 MSYS2 MINGW64 shell 下執行
cmake --preset Debug        # 或 Release
cmake --build --preset Debug
```

產出的 ELF/BIN/HEX 會位於 `build/Debug/`（或 `build/Release/`）。燒錄可透過 OpenOCD 或 STM32CubeProgrammer。

---

## Host 端（電腦端）

`Host/` 為監聽藍芽虛擬序列埠、解析 `CARD:XXXXXXXX` 封包並寫入 SQLite 的 Python 程式：

```bash
cd Host
python -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
cp .env.example .env    # 依實際序列埠裝置修改 SERIAL_PORT
python main.py
```

藍芽配對、`.env` 設定細節與疑難排解，可依上方步驟操作；`.env.example` 內含各參數的預設值與說明。

---

## 授權

未指定（依專案需求自行補充）。
