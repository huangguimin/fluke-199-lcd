# LCD信号转换器连接指南

## 概述
此项目将X3501 LCD的并行信号转换为ST7789 SPI LCD的显示输出。

## 硬件连接

### X3501 LCD 输入信号 (监听模式)
```
X3501 LCD 模块    →    Pico GPIO
─────────────────────────────────
LCDONOFF (pin 15) →   GPIO 1     ← LCD开关信号
FRAME (pin 5)    →    GPIO 2     ← 帧同步信号
LINECLK (pin 7)  →    GPIO 3     ← 行时钟信号  
DATACLK0 (pin 14) →   GPIO 4     ← 数据时钟
LCDAT0 (pin 8)   →    GPIO 5     ← 数据位0
LCDAT1 (pin 10)  →    GPIO 6     ← 数据位1
LCDAT2 (pin 11)  →    GPIO 7     ← 数据位2
LCDAT3 (pin 13)  →    GPIO 8     ← 数据位3
```

### ST7789 SPI LCD 输出
```
ST7789 LCD       →    Pico GPIO    →    功能
─────────────────────────────────────────────
VCC              →    3V3          →    电源 (3.3V)
GND              →    GND          →    地线
CS               →    GPIO 17      →    片选信号
DC/RS            →    GPIO 16      →    数据/命令选择
RST              →    GPIO 20      →    复位信号
BLK              →    GPIO 21      →    背光控制 (高电平点亮)
SCK/CLK          →    GPIO 18      →    SPI时钟 (20MHz)
SDA/MOSI         →    GPIO 19      →    SPI数据输出
```
