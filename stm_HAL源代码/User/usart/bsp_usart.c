#include "usart/bsp_usart.h"

UART_HandleTypeDef huart1;  // USART1 句柄

/**
  * @brief  调试串口 UART 初始化函数
  * @note   配置 USART1 的波特率、数据位、停止位、校验位与工作模式
  * @param  无
  * @retval 无
  */
void Debug_UART_Init(void)
{
    // 设置 USART1 相关参数
    huart1.Instance        = USART1;                   // USART1 外设
    huart1.Init.BaudRate   = 115200;                   // 波特率设置
    huart1.Init.WordLength = UART_WORDLENGTH_8B;       // 8 位数据位
    huart1.Init.StopBits   = UART_STOPBITS_1;          // 1 位停止位
    huart1.Init.Parity     = UART_PARITY_NONE;         // 无奇偶校验
    huart1.Init.Mode       = UART_MODE_TX_RX;          // 打开发送和接收
    huart1.Init.HwFlowCtl  = UART_HWCONTROL_NONE;      // 无硬件流控
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;   // 16 倍过采样

    // 初始化 UART
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler(); // 初始化失败，进入错误处理函数
    }
}


/**
  * @brief  USART1 外设底层初始化（由 HAL_UART_Init 自动调用）
  * @param  uartHandle：UART 句柄
  * @retval 无
  */
void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (uartHandle->Instance == USART1)
    {
        // 1. 使能 USART1 时钟
        __HAL_RCC_USART1_CLK_ENABLE();

        // 2. 使能 GPIOA 时钟（PA9、PA10）
        __HAL_RCC_GPIOA_CLK_ENABLE();

        // 3. 配置 USART1_TX（PA9）为复用推挽输出，高速
        GPIO_InitStruct.Pin   = GPIO_PIN_9;
        GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;              // 复用推挽输出
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;         // 高速
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        // 4. 配置 USART1_RX（PA10）为浮空输入
        GPIO_InitStruct.Pin  = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;               // 输入模式
        GPIO_InitStruct.Pull = GPIO_NOPULL;                   // 无上拉/下拉
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        // 5. 开启 USART1 中断（用于接收指令）
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}


/**
  * @brief  UART MSP 反初始化（释放资源）
  * @param  uartHandle：UART 句柄
  * @retval 无
  */
void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{
    if (uartHandle->Instance == USART1)
    {
        /*-------------------- 关闭外设时钟 --------------------*/
        __HAL_RCC_USART1_CLK_DISABLE();

        /*-------------------- 释放 GPIO 引脚 --------------------*/
        // PA9（USART1_TX）、PA10（USART1_RX）解除初始化
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
    }
}

/**
  * @brief  重定向 printf 的输出到串口
  * @param  ch 要发送的字符
  * @param  f  文件指针（标准库要求的参数，一般不使用）
  * @retval 返回发送的字符
  */
int fputc(int ch, FILE *f)
{
         HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
         return ch;
}

