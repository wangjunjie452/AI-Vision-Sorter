/**
 * @file    main.c
 * @brief   AI 视觉分拣系统 — STM32 主程序
 *
 * @details 系统工作流程：
 *          1. 上电初始化所有外设（LED、蜂鸣器、串口、I2C、舵机）
 *          2. LED 亮 + 蜂鸣器响一声 → 表示系统就绪
 *          3. 主循环等待 PC/K210 通过串口发来的分拣指令
 *          4. 收到指令 → 解析类别 → 执行对应舵机动作 → 回复 ACK/READY
 *          5. 每 5 秒检查一次心跳，10 秒无心跳则 LED 灭进入待机
 *
 * @note    引脚分配：
 *          PA9/PA10 = USART1（串口通信）
 *          PB6/PB7  = I2C1（PCA9685 舵机驱动）
 *          PC13     = 板载 LED（低电平亮）
 *          PA1      = 蜂鸣器（高电平响）
 *
 * @note    通信协议：
 *          PC → STM32:  "CMD:类别ID,X,Y\n"  或  "PING\n"
 *          STM32 → PC:  "ACK:类别ID\n" + 舵机动作 + "READY\n"  或  "PONG\n"
 */

#include "main.h"
#include "led/bsp_led.h"          /* 板载 LED 驱动 */
#include "dwt/bsp_dwt.h"          /* DWT 精确延时 */
#include "usart/bsp_usart.h"      /* USART1 串口驱动 */
#include "buzzer/bsp_buzzer.h"    /* 蜂鸣器驱动 */
#include "i2c/bsp_i2c.h"          /* I2C1 驱动（PCA9685 通信） */
#include "serial/serial_parser.h" /* 串口指令解析（环形缓冲区+协议） */
#include "servo/servo_ctrl.h"     /* 舵机控制（PCA9685 驱动） */

/* 函数前向声明 */
void SystemClock_Config(void);

int main(void)
{
	/* ===== 第一步：HAL 库初始化 + 时钟配置 ===== */
	HAL_Init();                         /* 初始化 HAL 库（SysTick、NVIC 等） */
	SystemClock_Config();               /* 配置系统时钟：HSE 8MHz → PLL → 72MHz */
	SystemCoreClock = 72000000;         /* 手动更新时钟变量（兼容 Keil 仿真） */

	/* ===== 第二步：外设硬件初始化 ===== */
	LED_GPIO_Config();                  /* PC13 LED 初始化（推挽输出，默认灭） */
	Buzzer_GPIO_Config();               /* PA1 蜂鸣器初始化（推挽输出，默认低） */
	Debug_UART_Init();                  /* USART1 初始化（115200, 8N1，PA9/PA10） */
	DWT_Init();                         /* DWT 计数器初始化（精确微秒延时） */
	MX_I2C1_Init();                     /* I2C1 初始化（100kHz，PB6/PB7） */

	/* ===== 第三步：用户模块初始化 ===== */
	Serial_Init();   /* 串口解析模块：清空环形缓冲区，开启 USART1 中断接收 */
	Servo_Init();    /* 舵机模块：PCA9685 设 50Hz，舵机归位 */

	/* ===== 第四步：舵机校准扫描（验证左右行程） ===== */
	Servo_Calibrate(1500);  /* 每个位置停留 1500ms */

	/* 系统就绪提示 */
	LED_ON();
	Buzzer_Beep(500);
	Serial_SendString("READY\n");

	/* ===== 第五步：主循环（非阻塞架构） ===== */
	while (1)
	{
		/* 检查是否收到完整指令（以 '\n' 结尾） */
		if (Serial_HasCommand()) {
			Cmd_t cmd = Serial_ParseCommand();

			/* classId == -99 是心跳 PING，已自动回复 PONG，跳过不执行动作 */
			if (cmd.classId == -99) continue;

			/* 只有舵机空闲时才接受新指令（防抖动） */
			if (Servo_IsIdle()) {
				LED_TOGGLE();                   /* LED 闪烁 = 正在处理 */
				Serial_SendAck(cmd.classId);    /* 回复 "ACK:类别ID" 告知 PC 已收到 */
				Servo_Execute(cmd);             /* 启动状态机（非阻塞，立即返回） */
			}
		}

		/* 舵机状态机更新：检查时间是否到达，切换下一步 */
		if (Servo_Update()) {
			/* Servo_Update 返回 1 = 动作全部完成 */
			Serial_SendReady();                 /* 回复 "READY" 表示可接受下一条指令 */
			LED_TOGGLE();                       /* LED 恢复 */
		}

		/* 心跳超时检测：10 秒没收到 PING → LED 灭（进入待机） */
		Serial_CheckHeartbeat();
	}
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow : 
  *            System Clock source            = PLL (HSE)
  *            SYSCLK(Hz)                     = 72000000
  *            HCLK(Hz)                       = 72000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 2
  *            APB2 Prescaler                 = 1
  *            HSE Frequency(Hz)              = 8000000
  *            HSE PREDIV1                    = 1
  *            PLLMUL                         = 9
  *            Flash Latency(WS)              = 2
  * @param  None
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef clkinitstruct = {0};
  RCC_OscInitTypeDef oscinitstruct = {0};
  
  /* Enable HSE Oscillator and activate PLL with HSE as source */
  oscinitstruct.OscillatorType  = RCC_OSCILLATORTYPE_HSE;
  oscinitstruct.HSEState        = RCC_HSE_ON;
  oscinitstruct.HSEPredivValue  = RCC_HSE_PREDIV_DIV1;
  oscinitstruct.PLL.PLLState    = RCC_PLL_ON;
  oscinitstruct.PLL.PLLSource   = RCC_PLLSOURCE_HSE;
  oscinitstruct.PLL.PLLMUL      = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&oscinitstruct)!= HAL_OK)
  {
    /* Initialization Error */
    while(1);
  }

  /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2 
     clocks dividers */
  clkinitstruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  clkinitstruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clkinitstruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clkinitstruct.APB2CLKDivider = RCC_HCLK_DIV1;
  clkinitstruct.APB1CLKDivider = RCC_HCLK_DIV2;  
  if (HAL_RCC_ClockConfig(&clkinitstruct, FLASH_LATENCY_2)!= HAL_OK)
  {
    /* Initialization Error */
    while(1);
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
