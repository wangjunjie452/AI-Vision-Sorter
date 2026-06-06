# CODEBUDDY.md This file provides guidance to CodeBuddy when working with code in this repository.

## 项目概述

STM32F103C8T6 HAL 库模板工程（野火开发板），基于 Keil MDK-ARM V5 + STM32CubeMX HAL 驱动。系统时钟 72MHz（HSE 8MHz → PLL ×9），输出目录 `Project/Objects/`。

## 构建与编译

- **IDE**：Keil MDK-ARM V5（ARMCC V5.06），工程文件 `Project/Fire_F103.uvprojx`
- **器件包**：Keil.STM32F1xx_DFP.2.2.0
- **编译产物**：`Project/Objects/Fire_F103.axf`（调试）、`Fire_F103.hex`（烧录）
- **全局宏定义**：`USE_HAL_DRIVER, STM32F103xB`
- **C 标准**：C99
- **编译器优化**：`-O1`，`--no_multibyte_chars`

## 目录结构

```
├── Doc/                     # 文档与硬件接线图
├── Libraries/
│   ├── CMSIS/               # CMSIS Core (armcc) + Device (STM32F1xx)
│   │   ├── Device/ST/STM32F1xx/
│   │   │   ├── Include/     # stm32f103xb.h, system_stm32f1xx.h
│   │   │   └── Source/Templates/arm/
│   │   │       └── startup_stm32f103xb.s   # 启动文件（向量表+SystemInit）
│   │   └── Include/         # core_cm3.h, cmsis_armcc.h 等
│   └── STM32F1xx_HAL_Driver/
│       ├── Inc/             # HAL 外设头文件
│       └── Src/             # HAL 外设实现（当前启用: gpio/rcc/cortex/dma/uart/usart）
├── Project/                 # Keil 工程文件与编译产物
├── User/                    # 用户代码（核心）
│   ├── main.c / main.h      # 入口与全局头文件
│   ├── stm32f1xx_it.c/.h    # 中断服务函数
│   ├── stm32f1xx_hal_conf.h # HAL 模块裁剪开关
│   ├── system_stm32f1xx.c   # CMSIS 系统初始化（SystemInit/SystemCoreClockUpdate）
│   ├── dwt/bsp_dwt.c/.h     # DWT 精确延时
│   ├── led/bsp_led.c/.h     # RGB LED 驱动
│   └── usart/bsp_usart.c/.h # 调试串口 USART1
└── CODEBUDDY.md
```

## 架构设计

### 启动流程

1. **`startup_stm32f103xb.s`**：上电复位 → 调用 `SystemInit()`（`system_stm32f1xx.c`，配置 Flash 延迟 + HSI 时钟）→ 跳转 `main()`
2. **`main()`**：`HAL_Init()` → `SystemClock_Config()` → 外设初始化 → `while(1)` 主循环

### 模块分层

| 层 | 目录 | 职责 |
|---|---|---|
| 底层 | `Libraries/CMSIS` | 启动、寄存器定义、系统时钟 |
| HAL 驱动 | `Libraries/STM32F1xx_HAL_Driver` | 外设抽象（GPIO/UART/RCC/DMA） |
| BSP 板级 | `User/dwt`, `User/led`, `User/usart` | 硬件板级驱动封装 |
| 应用 | `User/main.c`, `APP/`（预留） | 业务逻辑 |

### 外设与引脚

| 外设 | 引脚 | 用途 |
|---|---|---|
| USART1_TX | PA9（复用推挽） | 调试串口 TX，波特率 115200/8N1 |
| USART1_RX | PA10（浮空输入） | 调试串口 RX |
| RGB LED 红 | PA1（推挽输出，低电平亮） | 共阳 RGB LED |
| RGB LED 绿 | PA2（推挽输出，低电平亮） | 同上 |
| RGB LED 蓝 | PA3（推挽输出，低电平亮） | 同上 |

### key design decisions

1. **延时方案**：使用 DWT（`DWT_CYCCNT` 寄存器）实现微秒/毫秒/秒级精确延时，不依赖 SysTick。`DWT_DelayUs()` 基于 `SystemCoreClock` 计算每微秒的 tick 数，通过空转等待 CYCCNT 差值实现。
2. **printf 重定向**：`fputc()` 重定向到 `HAL_UART_Transmit(&huart1, ..., HAL_MAX_DELAY)`，阻塞式发送。
3. **LED 控制**：通过函数宏直接调用 `HAL_GPIO_WritePin/TogglePin`，初始化和控制在头文件中一体封装。共阳型 LED 低电平点亮。
4. **MSP 模式**：`HAL_UART_MspInit/MspDeInit` 在 UART 初始化/反初始化时自动由 HAL 回调，负责 GPIO 时钟和外设时钟的使能与释放。
5. **中断处理**：`SysTick_Handler` 调用 `HAL_IncTick()` 维持 HAL 时基。其余异常处理均为空或死循环占位。

### 仿真注意事项

Keil 仿真器无法模拟 PLL 的硬件行为，`SystemCoreClock` 默认保持为 8MHz（HSI），导致 `DWT_DelayUs` 延时变为实际的 9 倍。**在 `main()` 的 `SystemClock_Config()` 之后需手动设置 `SystemCoreClock = 72000000`** 以修正仿真时序。真机上 HAL 会自动覆盖，不受影响。
