# AI 视觉分拣系统

基于 YOLOv8 + STM32 的电子元器件自动分拣系统。摄像头实时检测电阻、LED、电容，通过串口指令控制舵机完成分类。

## 系统架构

```
摄像头采集 → YOLOv8 目标检测 → 串口指令 → STM32 控制 → 舵机分拣
  (PC)          (PC)         UART 115200    (STM32)     (PCA9685)
```

## 分拣类别

| 类别 | 识别物体 | 分拣动作 |
|------|---------|---------|
| A (0) | 电阻 resistor | 入口放行 |
| B (1) | LED | 末端左分拣 |
| C (2) | 电容 capacitor | 末端右分拣 |

## 快速开始

### PC 端

```bash
cd pc
pip install -r requirements.txt

# 运行检测 + 分拣
python detect.py --port COM6 --cam 1

# 摄像头 0 = 笔记本内置，1 = iVCam 手机
# 按 ESC 退出
```

### STM32 固件

- IDE: Keil MDK-ARM v5
- 工程文件: `stm_HAL源代码/Project/Fire_F103.uvprojx`
- 编译: F7 (Build)
- 烧录: SWD 或 UART ISP

## 目录结构

```
├── pc/                          # PC 端 Python 程序
│   ├── detect.py                # 主程序（YOLO 检测 + 串口通信 + 状态机）
│   ├── serial_comm.py           # 串口通信模块（心跳保活）
│   ├── custom_electronics.pt    # 训练好的 YOLO 模型（mAP50=86.8%）
│   ├── collect_data.py          # 数据集采集工具
│   ├── annotate.py              # 图片标注工具
│   ├── train.py                 # 模型训练脚本
│   ├── color_detect.py          # HSV 颜色检测（备用方案）
│   ├── test_camera.py           # 摄像头测试
│   ├── test_yolo.py             # YOLO 检测测试
│   ├── data.yaml                # 数据集配置
│   └── dataset/                 # 训练数据集
│       ├── images/
│       └── labels/
│
├── stm_HAL源代码/               # STM32 固件（C / HAL 库）
│   ├── User/
│   │   ├── main.c               # 主程序入口
│   │   ├── serial/serial_parser # 串口协议解析（环形缓冲区 + 心跳）
│   │   ├── servo/servo_ctrl     # 舵机控制（PCA9685 + 非阻塞状态机）
│   │   ├── i2c/bsp_i2c          # I2C 驱动
│   │   ├── usart/bsp_usart      # USART 驱动
│   │   ├── led/bsp_led          # LED 驱动
│   │   ├── buzzer/bsp_buzzer    # 蜂鸣器驱动
│   │   └── dwt/bsp_dwt          # DWT 精确延时
│   ├── Libraries/               # STM32 HAL + CMSIS
│   └── Project/Keil 工程文件
│
├── hardware(原理图_PCB)/         # 硬件设计（EasyEDA）
├── 项目书/                      # 技术文档、接线图、项目管理
└── CLAUDE.md                    # AI 辅助开发文档
```

## 通信协议

文本协议，115200 波特率，换行符结尾：

| 方向 | 消息 | 含义 |
|------|------|------|
| PC → STM32 | `CMD:<类别>,<x>,<y>\n` | 分拣指令 |
| PC → STM32 | `PING\n` | 心跳（每 5s） |
| STM32 → PC | `ACK:<类别>\n` | 指令确认 |
| STM32 → PC | `READY\n` | 舵机完成，可接受下一指令 |
| STM32 → PC | `PONG\n` | 心跳回复 |

## PC 端状态机

```
IDLE ──发送 CMD──→ WAIT_ACK ──收到 ACK──→ WAIT_READY ──收到 READY──→ IDLE
                          (2s 超时)                  (5s 超时)
```

同一类别分拣后有 5s 冷却期，防止同一物体反复触发。

## 模型训练

使用 YOLOv8n 在自定义数据集上微调：

```bash
cd pc

# 1. 采集图片
python collect_data.py --cam 1

# 2. 标注图片
python annotate.py --dir dataset/images/train --save dataset/labels/train

# 3. 训练
python train.py --epochs 50

# 4. 复制模型
copy runs\detect\train\weights\best.pt custom_electronics.pt
```

训练结果: mAP50 = 86.8%, Precision = 83.3%, Recall = 76.2%

## 硬件清单

| 器件 | 型号 | 用途 |
|------|------|------|
| 主控 | STM32F103C8T6 | 系统控制 |
| 舵机驱动 | PCA9685 | I2C 舵机控制 |
| 舵机 | SG90 × 2 | 挡板 + 分拣臂 |
| 串口 | USB-TTL CH340 | PC 通信 |
| 电源 | 5V 2A | 舵机供电 |

## 引脚分配

| 外设 | 引脚 | 用途 |
|------|------|------|
| USART1 TX | PA9 | 串口发送 |
| USART1 RX | PA10 | 串口接收 |
| I2C1 SCL | PB6 | PCA9685 时钟 |
| I2C1 SDA | PB7 | PCA9685 数据 |
| LED | PC13 | 状态指示 |
| Buzzer | PA1 | 启动提示音 |
