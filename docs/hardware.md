# Hardware

| Component                  | Features                                                                 | Chip/Model            |
|----------------------------|--------------------------------------------------------------------------|-----------------------|
| M5Stack CoreS3             |                                                                          |                       |
| - MCU                      | 16M FLASH, 8M PSRAM, WiFi, OTG&CDC functions                             | ESP32-S3@Xtensa LX7   |
| - Touch IPS LCD screen     | 2.0" 320x240, capacitive touch, IPS screen                               | ILI9342C              |
| - Camera                   | 0.3 megapixels                                                           | GC0308                |
| - Proximity sensors        | Ambient light and proximity detection                                    | LTR-553ALS-WA         |
| - Power management         | 4-way power flow control loop                                            | AXP2101               |
| - Power Supply             | External DC 12V (supports 9~24V) or internal 500mAh lithium battery      |                       |
| - Six-axis attitude sensor | Accelerometer and gyroscope                                              | BMI270                |
| - Magnetometer             | Magnetic field sensor                                                    | BMM150                |
| - RTC                      | Real-time clock                                                          | BM8563                |
| - Speaker                  | 1W, 16-bit I2S power amplifier                                           | AW88298               |
| - Audio decoding chip      | Dual microphone inputs                                                   | ES7210                |
| - Power & Reset Buttons    | Includes self-built delay circuit                                        |                       |
| - Inputs                   | USB-C (OTG), I2C (Port A), GPIO (Port B), UART (Port C)                  |                       |
| - microSD Card             | Onboard data storage for logs and readings                               |                       |
| - Mounting Options         | DinBase Base included for Din rail, wall, and screw mounting             |                       |
| - Expansion                | DinBase provides multiple proto locations for customization & expansion  |                       |
| - Product Size             | 54 x 54 x 16mm                                                           |                       |
| - Product Weight           | 73.3g                                                                    |                       |
| LilyGo T-Watch Ultra |                                                      |                       |
| - MCU                      | 16M QSPI FLASH, 8M PSRAM, 512K SRAM, WiFi 802.11 b/g/n, BLE 5.0          | ESP32-S3R8@Xtensa LX7 |
| - Display                  | 2.06" AMOLED, 410x502, 16.7M colours, capacitive touch                   | CO5300 / CST9217      |
| - LoRa                     | 915MHz variant (NZ ISM band 915–928MHz)                                  | Semtech SX1262        |
| - GNSS                     | Onboard multi-constellation receiver                                     | u-blox MIA-M10Q       |
| - IMU                      | Smart sensor hub (accelerometer/gyro, gesture wake)                      | BHI260AP              |
| - NFC                      | NFC reader/writer                                                        | ST25R3916             |
| - RTC                      | Real-time clock                                                          | PCF85063A             |
| - Audio                    | Microphone + I2S Class-D speaker amplifier                               | MAX98357A             |
| - Haptics                  | Vibration driver (ERM/LRA)                                               | DRV2605               |
| - Inputs                   | Touch, 2 usable buttons (PWR via PMU IRQ, GPIO0 custom), reset (non-programmable) |              |
| - Power management         | Power management unit (PWR button events via IRQ)                        | AXP2101               |
| - Storage                  | microSD card slot                                                        |                       |
| - Power                    | 1100mAh battery (4.07Wh), USB-C charging                                 |                       |
| - Rating / Size            | IP65, 63.5 x 49 x 22mm                                                   |                       |
| - Development Platforms    | Arduino IDE, PlatformIO, ESP-IDF, MicroPython (LilyGoLib)                |                       |
| M5Stack GPS Module v2      |                                                                          |                       |
| - GNSS Chip                | High-performance, multi-frequency, multi-system GNSS receiver            | AT6668                |
| - Supported Systems        | GPS, QZSS, BD2, BD3, GALILEO, GLONASS                                    |                       |
| - Multi-channel Reception  | Simultaneous multi-system joint or single-system independent positioning |                       |
| - Antenna                  | External active GPS/BD antenna (SMA, 1m cable included)                  |                       |
| - DIP Switch               | Flexible TX/RX pin selection, PPS timing output                          |                       |
| - Stacking Support         | Multiple module stacking for customization                               |                       |
| - Low Power Consumption    | Optimized for battery-powered applications                               |                       |
| - Development Platforms    | UiFlow1, UiFlow2, Arduino IDE                                            |                       |
| - Product Name / SKU       | Module GPS v2.0 / M003-V2                                                |                       |

### ESP32S3 GPIO list

| GPIO    | Function                   |
|---------|----------------------------|
| GPIO 0  | M-Bus **SPK_LRCK**         |
| GPIO 1  | PORT.A **I2C0_SCL**        |
| GPIO 2  | PORT.A **I2C0_SDA**        |
| GPIO 3  | **LCD_CS**                 |
| GPIO 4  | **TF_CS**                  |
| GPIO 5  | M-Bus                      |
| GPIO 6  | M-Bus                      |
| GPIO 7  | M-Bus                      |
| GPIO 8  | M-Bus, PORT.B              |
| GPIO 9  | M-Bus, PORT.B              |
| GPIO10  | M-Bus                      |
| GPIO11  | **I2C1_SCL**               |
| GPIO12  | **I2C1_SDA**               |
| GPIO13  | M-Bus **SPK_D**            |
| GPIO14  | M-Bus **MIC_IN**           |
| GPIO15  | **CAM_D6**                 |
| GPIO16  | **CAM_D7**                 |
| GPIO17  | M-Bus, PORT.C              |
| GPIO18  | M-Bus, PORT.C              |
| GPIO19  | USB **D--**                |
| GPIO20  | USB **D++**                |
| GPIO21  | **I2C_INT**                |
| GPIO33  | **SPK_WCK**                |
| GPIO34  | **SPK_BCK**                |
| GPIO35  | M-Bus **SPI_MISO, LCD DC** |
| GPIO36  | M-Bus **SPI_SCLK**         |
| GPIO37  | M-Bus **SPI_MOSI**         |
| GPIO38  | **CAM_HREF**               |
| GPIO39  | **CAM_D2**                 |
| GPIO40  | **CAM_D3**                 |
| GPIO41  | **CAM_D4**                 |
| GPIO42  | **CAM_D5**                 |
| GPIO43  | M-Bus **SerialTX**         |
| GPIO44  | M-Bus **SerialRX**         |
| GPIO45  | **CAM_PCLK**               |
| GPIO46  | **CAM_VSYNC**              |
| GPIO47  | **CAM_D9**                 |
| GPIO48  | **CAM_D8**                 |

### AXP2101 Usage

| Power Rail    | CoreS3 Function |
|---------------|-----------------|
| ALDO1         | VDD 1v8         |
| ALDO2         | VDDA 3v3        |
| ALDO3         | CAM 3v3         |
| ALDO4         | TF 3v3          |
| BLDO1         | AVDD            |
| BLDO2         | DVDD            |
| DLDO1/DC1     | LCD BL          |
| DLDO2/DC2     | ---             |
| BACKUP        | RTC BAT         |

### I2C Devices

| Device                      | I2C Address     |
|-----------------------------|-----------------|
| Touch Panel                 | `FT5xxx` 38h    |
| RTC                         | `BM8563` 51h    |
| Power Management            | `AXP2101` 34h   |
| IMU                         | `BMI270` 69h    |
| ENV (Ambient/Proximity)     | `LTR553ALS` 23h |
| Camera                      | `GC0308` 21h    |
| Speaker                     | `AW88298` 36h   |
| Microphone                  | `ES7210` 40h    |
| GPIO Expander               | `AW9523B` 58h   |
