# Smart Door Lock — 智能门锁

基于 BLE 通信的智能门锁系统。手机 App 开锁 + 密码键盘开锁 + 云端密钥管理。

## 系统架构

```
手机 App (Android)
  │  BLE Nordic UART Service
  │  6E400001/002/003 UUID
  │  Write + Notify
ESP32-S3 (ESP-IDF + NimBLE)
  │  认证状态机 · 128-bit 密钥 · NVS 持久化
  │  UART 9600 波特
STC89C52RC (51 单片机)
  矩阵键盘 · OLED · AT24C02 EEPROM
```

## 硬件

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S3 |
| 面板 | STC89C52RC |
| 显示 | OLED 128×64 (SSD1306, I2C) |
| 存储 | AT24C02 (EEPROM, I2C) |
| 键盘 | 4×4 矩阵键盘 |
| 后端 | Python FastAPI + SQLite |

## 项目结构

```
smartdoorlock/
├── main/                            # ESP32 固件
│   ├── CMakeLists.txt
│   ├── main.c                       # 认证状态机
│   ├── drivers/ble_driver.c/h       # NimBLE BLE 驱动
│   ├── services/uart_service.c/h    # BLE UART 服务
│   └── hardware/uart_hw.c/h         # ↔ 51 串口通信
├── 51单片机端/                        # 51 单片机固件
│   ├── Project/main.c               # 入口
│   ├── App/MatrixKeyApp.c/h          # 键盘状态机 + OLED 菜单
│   ├── App/UartApp.c/h              # 串口命令处理
│   ├── Hardware/                    # OLED, MatrixKey, AT24C02, UART
│   ├── System/                      # Scheduler, Delay
│   └── Tools/gen_font.py            # 字库生成
├── android_app/                     # Android App
│   ├── app/src/main/java/com/smartdoorlock/app/
│   │   ├── BleManager.kt            # BLE 扫描/连接/GATT
│   │   ├── DoorLockApi.kt           # REST API 客户端
│   │   ├── MainActivity.kt
│   │   └── ui/                      # Compose UI
│   └── build.gradle.kts
├── web_app/                         # 后端服务
│   └── backend/
│       ├── main.py                  # FastAPI (JWT + SQLite)
│       ├── Dockerfile
│       └── requirements.txt
├── tools/                           # PC 调试工具
│   ├── ble_uart_assistant.py        # GUI BLE 调试助手
│   ├── ble_test.py                  # 连通性测试
│   └── ble_uart/                    # BLE UART Python 库
├── sdkconfig.defaults               # ESP-IDF 配置
├── CMakeLists.txt                   # ESP-IDF 项目根
├── .devcontainer/                   # Dev Container (ESP-IDF)
└── .github/workflows/               # CI (Android APK 构建)
```

## 通信协议

### BLE 服务

| UUID | 属性 | 方向 |
|------|------|------|
| `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Service | — |
| `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write | 手机 → ESP32 |
| `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify | ESP32 → 手机 |

### 命令

**手机 → ESP32:**
`[HELLO]` · `[AUTH] <32位hex密钥>` · `[UNPAIR]` · `[PWD_QUERY]` · `[PWD_SET:xxxxxx]`

**ESP32 → 手机:**
`[BOND] <32位hex密钥>` · `[READY]` · `[OK]` · `[FAIL]` · `[UNPAIRED]`

**ESP32 ↔ 51 单片机 (UART):**
`[PAIR_OK]` · `[UNLOCK]` · `[PWD_DATA:xxxxxx]` · `[PWD_OK]` · `[PWD_ERR]` · `ENTER_PAIR`

## 构建

### ESP32
```bash
. $HOME/esp/esp-idf/export.sh
idf.py build
idf.py -p COM3 flash monitor
```

### Android
Android Studio 打开 `android_app/` 目录，同步 Gradle 后运行。

### 后端
```bash
cd web_app/backend && pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
# 或 Docker
cd web_app && docker compose up -d
```

### 51 单片机
Keil μVision 打开 `51单片机端/Project/Project.uvproj`，编译烧录。

### 调试
```bash
pip install -r tools/requirements.txt
python tools/ble_uart_assistant.py
```

## License

MIT
