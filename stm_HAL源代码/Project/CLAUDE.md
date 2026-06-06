# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## 1. 工程概览

- **MCU:** STM32F103C8 (Cortex-M3, 72MHz, 128KB Flash, 20KB SRAM)
- **IDE:** Keil MDK-ARM v5 (uVision), ARMCC V5.06 update 7
- **工程文件:** `Project/Fire_F103.uvprojx` (目标名: `HAL`)
- **输出产物:** `Project/Objects/Fire_F103.hex` (HEX-80格式)
- **实验平台:** 野火 STM32F103C8T6 开发板
- **功能:** DWT 精确延时驱动 DHT11 位带协议，USART1 串口打印温湿度数据

---

## 2. 目录结构

```
├── APP/dht11/         应用层 — 高级业务逻辑，组装BSP调用并输出可读结果
│   ├── app_dht11.c    DHT11读取并打印的综合函数
│   └── app_dht11.h    函数声明
│
├── User/              入口 + BSP驱动层 — 硬件初始化与底层IO
│   ├── main.c         主入口 (初始化 → while循环调APP函数)
│   ├── main.h         工程总头文件 (含 HAL + stdio)
│   ├── dwt/bsp_dwt.c  DWT周期计数器驱动 (微秒级精确延时)
│   ├── dwt/bsp_dwt.h  DWT API声明
│   ├── dht11/bsp_dht11.c  DHT11位带协议驱动 (依赖DWT)
│   ├── dht11/bsp_dht11.h  DHT11 API / 引脚宏 / 状态码枚举
│   ├── usart/bsp_usart.c  USART1初始化 + fputc重定向 (printf→串口)
│   ├── usart/bsp_usart.h  USART1 API声明
│   ├── led/bsp_led.c   LED GPIO驱动 (当前main未引用)
│   ├── led/bsp_led.h   LED API声明
│   ├── stm32f1xx_it.c  中断服务 (SysTick→HAL_IncTick)
│   ├── stm32f1xx_it.h  中断服务声明
│   ├── stm32f1xx_hal_conf.h  HAL模块裁剪宏
│   └── system_stm32f1xx.c    CMSIS系统初始化
│
└── Libraries/         STM32F1xx HAL + CMSIS (CubeMX生成的标准库)
    ├── CMSIS/         Core + Device (startup / system)
    └── STM32F1xx_HAL_Driver/  HAL外设驱动 (uart/gpio/tim/dma/...)
```

---

## 3. 模块调用链

```
main.c
  ├── HAL_Init()                     ← HAL库
  ├── SystemClock_Config()           ← 72MHz (HSE 8MHz × PLL9)
  ├── Debug_UART1_Init()             ← bsp_usart → HAL_UART_Init → HAL_UART_MspInit
  ├── DWT_Init()                     ← bsp_dwt → 使能DWT_CYCCNT
  │
  └── while(1):
        Dht11_ReadAndPrint()          ← app_dht11
          └── DHT11_Read()            ← bsp_dht11
                ├── DWT_DelayMs()     ← bsp_dwt  (起始信号 >18ms)
                ├── DWT_DelayUs()     ← bsp_dwt  (拉高 20~40us)
                ├── DHT11_Read_Byte() ← bsp_dht11(static, 位时序用DWT_GetTick)
                └── DHT11_GPIO_Mode_Config() ← bsp_dht11 (切换输入/输出)
```

依赖层级: `APP → User/BSP → HAL/CMSIS`。APP 只依赖 BSP 提供的公开API和 `main.h`。

---

## 4. 各模块 API 清单

### 4.1 DWT 模块

| 文件 | 路径 |
|------|------|
| 源文件 | [bsp_dwt.c](User/dwt/bsp_dwt.c) |
| 头文件 | [bsp_dwt.h](User/dwt/bsp_dwt.h) |

**对外 API:**

| 函数 | 功能 |
|------|------|
| `void DWT_Init(void)` | 使能 DWT 周期计数器（TRCENA → CYCCNT → 清空计数），72MHz下每 tick = 1/72us |
| `uint32_t DWT_GetTick(void)` | 返回当前 DWT_CYCCNT 值（单位：CPU时钟周期） |
| `void DWT_DelayUs(uint32_t us)` | 微秒级阻塞延时，基于 DWT_GetTick 自旋 |
| `void DWT_DelayMs(uint32_t ms)` | 毫秒级阻塞延时，内部调用 DWT_DelayUs × 1000 |

**注意:** 真机上 `SystemCoreClock = 72000000` 由 HAL_RCC_ClockConfig 自动更新。Keil 仿真器无法模拟 PLL，需手动设置 `SystemCoreClock = 72000000`，否则延时误差为 9 倍。

### 4.2 DHT11 模块

| 文件 | 路径 |
|------|------|
| 源文件 | [bsp_dht11.c](User/dht11/bsp_dht11.c) |
| 头文件 | [bsp_dht11.h](User/dht11/bsp_dht11.h) |

**引脚宏:**

| 宏 | 值 |
|------|-----|
| `DHT11_DATA_GPIO_Port` | `GPIOB` |
| `DHT11_DATA_Pin` | `GPIO_PIN_12` |

**状态码枚举 `DHT11_Status`:**

| 值 | 含义 |
|------|------|
| `DHT11_OK` | 读取成功 |
| `DHT11_ERR_START_TIMEOUT` | 起始信号超时（DHT11 未响应） |
| `DHT11_ERR_BIT_TIMEOUT` | 数据位读取超时 |
| `DHT11_ERR_CHECKSUM` | 校验和错误 |

**对外 API:**

| 函数 | 功能 |
|------|------|
| `void DHT11_GPIO_Config(void)` | 初始化 PB12 为推挽输出，默认拉高 |
| `void DHT11_GPIO_Mode_Config(uint32_t mode, uint32_t pull)` | 运行时切换引脚模式（输入/输出） |
| `DHT11_Status DHT11_Read(uint8_t *humidity, uint8_t *temperature)` | 完整 40bit 协议读取，返回湿度(整数%)和温度(整数°C) |

**内部 static 函数:** `DHT11_Read_Byte()` — 逐位读取 8bit，用 DWT_GetTick 测量高电平持续时间区分 bit 0/1（>40us 判定为 bit 1）。

### 4.3 USART 模块

| 文件 | 路径 |
|------|------|
| 源文件 | [bsp_usart.c](User/usart/bsp_usart.c) |
| 头文件 | [bsp_usart.h](User/usart/bsp_usart.h) |

**引脚:** TX=PA9, RX=PA10, 波特率=115200, 8N1, 无流控

**对外 API:**

| 函数/变量 | 功能 |
|------|------|
| `extern UART_HandleTypeDef huart1` | UART1 HAL 句柄，供其他模块引用 |
| `void Debug_UART1_Init(void)` | 初始化 USART1，配置 GPIO 并启用时钟 |
| `int fputc(int ch, FILE *f)` | 重定向 stdout → USART1，使 `printf()` 输出到串口 |

**HAL 回调:** `HAL_UART_MspInit()` / `HAL_UART_MspDeInit()` — 时钟 + GPIO 初始化/反初始化。

### 4.4 LED 模块（备用）

| 文件 | 路径 |
|------|------|
| 源文件 | [bsp_led.c](User/led/bsp_led.c) |
| 头文件 | [bsp_led.h](User/led/bsp_led.h) |

当前 `main.c` 未引用此模块，预留用于状态指示。

### 4.5 APP 层：DHT11 综合应用

| 文件 | 路径 |
|------|------|
| 源文件 | [app_dht11.c](APP/dht11/app_dht11.c) |
| 头文件 | [app_dht11.h](APP/dht11/app_dht11.h) |

**对外 API:**

| 函数 | 功能 |
|------|------|
| `void Dht11_ReadAndPrint(void)` | 调用 `DHT11_Read()` → 按状态码中文打印温湿度或错误信息到串口 |

对应三种超时错误和校验和错误的区分打印，成功则输出 "湿度为 xx %RH，温度为 xx ℃"。

### 4.6 中断服务

| 文件 | 路径 |
|------|------|
| 源文件 | [stm32f1xx_it.c](User/stm32f1xx_it.c) |

核心: `SysTick_Handler()` → `HAL_IncTick()`（为 HAL_Delay 提供时基）。其余异常处理（HardFault/MemManage/BusFault/UsageFault）均为死循环。

---

## 5. 引脚分配一览

| 外设 | 信号 | GPIO | 说明 |
|------|------|------|------|
| USART1 | TX | PA9 | 调试串口发送 |
| USART1 | RX | PA10 | 调试串口接收 |
| DHT11 | DATA | PB12 | 单总线数据（模块外部上拉） |

---

## 6. 主函数结构（可修改区域）

[main.c](User/main.c) 当前结构：

```c
// === include 区 ===
#include "main.h"           // HAL + stdio + Error_Handler
#include "led/bsp_led.h"    // [备] LED驱动
#include "dwt/bsp_dwt.h"    // DWT精确延时
#include "usart/bsp_usart.h" // USART1 + printf重定向
#include "dht11/app_dht11.h" // 应用层DHT11封装

// === 初始化区 ===
HAL_Init();
SystemClock_Config();     // 72MHz
Debug_UART1_Init();       // 115200 8N1
DWT_Init();               // 使能DWT周期计数器

// === 主循环区 (修改此处添加/替换APP层调用) ===
while (1) {
    Dht11_ReadAndPrint();
    HAL_Delay(2000);V
}
```

在主循环中追加新功能时，遵循 `APP层封装 → main.c只调用APP头文件` 的模式。

---

## 7. 编译 & 烧录

- **编译:** Keil IDE → Project → Build Target (F7)，输出 `Objects/Fire_F103.hex`
- **烧录工具:** 开发板 USB 转串口 (CH340)，使用 FlyMcu 或串口 ISP 工具通过 USART1 的 PA9/PA10 下载
- **串口监视:** 波特率 115200, 8 数据位, 1 停止位, 无校验, 无流控