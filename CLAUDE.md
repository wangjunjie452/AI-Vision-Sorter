# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AI 视觉分拣系统 (AI Vision Sorting System) — a three-tier embedded system that uses camera-based object detection to sort items into categories via servo-controlled gates.

**Data flow:**
```
PC (YOLOv8 detection) → UART 115200/8N1 → STM32F103C8T6 → I2C 100kHz → PCA9685 → Servos
```

The system sorts detected objects into 3 categories:
- **Category A (0):** bottle, cup — pass-through gate
- **Category B (1):** cell phone, remote — divert left
- **Category C (2):** apple, banana — divert right

## Running the PC Application

```bash
# Install dependencies
pip install -r pc/requirements.txt

# Run main detection + sorting loop (requires camera + STM32 connected)
python pc/detect.py --port COM6                  # 默认摄像头 1 (iVCam)
python pc/detect.py --port COM3 --cam 0          # 笔记本内置摄像头
python pc/detect.py --port COM3 --no-show        # 不显示画面

# Test camera only
python pc/test_camera.py

# Test YOLO model only
python pc/test_yolo.py
```

The PC app has a state machine to sync with STM32: IDLE → send CMD → wait ACK → wait READY → IDLE. Timeout 5s if STM32 doesn't respond.

## Building the STM32 Firmware

- **IDE:** Keil MDK-ARM v5, project file: `stm_HAL源代码/Project/Fire_F103.uvprojx`
- **Build:** Open project in Keil → F7 (Build) → output: `Project/Objects/Fire_F103.hex`
- **Flash (SWD):** `STM32_Programmer_CLI -c port=SWD -w Project/Objects/Fire_F103.hex -v -rst`
- **Flash (UART ISP):** Use FlyMcu via USART1 (PA9/PA10)
- **MCU:** STM32F103C8 (Cortex-M3, 72MHz, 128KB Flash, 20KB SRAM)
- **Global defines:** `USE_HAL_DRIVER, STM32F103xB`, C99 standard

**⚠️ Outdated note:** `stm_HAL源代码/Project/CLAUDE.md` still describes the old DHT11 project, not the current servo control firmware. Do not rely on it for API reference.

## STM32 Firmware Architecture

Source code lives in `stm_HAL源代码/User/`. Layered BSP architecture:

| Layer | Directory | Role |
|-------|-----------|------|
| Libraries | `Libraries/` | CMSIS + STM32 HAL drivers (vendor-provided) |
| BSP Drivers | `User/{module}/bsp_*.c` | Hardware abstraction per peripheral |
| Application | `User/serial/`, `User/servo/` | Business logic (protocol parsing, servo state machine) |
| Entry | `User/main.c` | Init sequence + non-blocking main loop |

**Modules in `User/`:**
- `serial/serial_parser.c` — Ring buffer (128B), interrupt-driven UART receive, CMD/PING protocol, heartbeat timeout (10s)
- `servo/servo_ctrl.c` — PCA9685 I2C driver (addr 0x40), non-blocking state machine for 3 sort sequences (A/B/C)
- `i2c/bsp_i2c.c` — I2C1 init (100kHz)
- `usart/bsp_usart.c` — USART1 init (115200), `printf` redirection via `fputc`
- `led/bsp_led.c` — Onboard LED (PC13, active-low)
- `buzzer/bsp_buzzer.c` — Buzzer (PA1)
- `dwt/bsp_dwt.c` — DWT cycle counter for microsecond-precision delays

**Main loop pattern (non-blocking):**
```c
while (1) {
    if (Serial_HasCommand())   → parse CMD → Servo_Execute() (launches state machine)
    if (Servo_Update())        → returns 1 when sequence done → send "READY\n"
    Serial_CheckHeartbeat()    → 10s timeout → LED off
}
```

## UART Communication Protocol

Text-based, 115200 baud, newline-terminated:

| Direction | Message | Meaning |
|-----------|---------|---------|
| PC → STM32 | `CMD:<classId>,<x>,<y>\n` | Sort command (classId: 0/1/2) |
| PC → STM32 | `PING\n` | Heartbeat (sent every 5s) |
| STM32 → PC | `ACK:<classId>\n` | Command received |
| STM32 → PC | `READY\n` | Servo sequence complete, ready for next |
| STM32 → PC | `PONG\n` | Heartbeat response |
| STM32 → PC | `ERR\n` | Parse error |

## Pin Assignments

| Peripheral | Pin | Purpose |
|-----------|-----|---------|
| USART1 TX | PA9 | Serial to PC |
| USART1 RX | PA10 | Serial from PC |
| I2C1 SCL | PB6 | PCA9685 clock |
| I2C1 SDA | PB7 | PCA9685 data |
| LED | PC13 | Status indicator (active-low) |
| Buzzer | PA1 | Startup beep (active-high) |

## Key Design Decisions

1. **Non-blocking state machine** in `servo_ctrl.c` — servo sequences use timestamp checks (`HAL_GetTick`) instead of `HAL_Delay()` to keep UART responsive during multi-step sorting.
2. **Heartbeat mechanism** — PC sends `PING` every 5s; STM32 turns off LED after 10s of silence, signaling connection loss.
3. **DWT for precise delays** — Used only during servo calibration scan at boot. Normal operation uses `HAL_GetTick`.
4. **Keil simulation caveat** — Simulator can't emulate PLL; `SystemCoreClock` defaults to 8MHz (HSI). Code manually sets `SystemCoreClock = 72000000` after clock config for simulation compatibility.
