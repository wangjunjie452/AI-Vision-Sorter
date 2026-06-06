#include "i2c/bsp_i2c.h"

I2C_HandleTypeDef hi2c1;  // 定义I2C句柄变量

/**
  * @brief  初始化 I2C1 
  * @note   I2C1 的引脚配置：SDA -> PB7，SCL -> PB6
  */
void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;                      // 选择使用的I2C外设为I2C1
    hi2c1.Init.ClockSpeed = 100000;            // 设置I2C时钟频率为100kHz（PCA9685 标准模式）
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;   // 设置时钟占空比为2（标准模式常用）
    hi2c1.Init.OwnAddress1 = 0;                // 主机模式下自定义地址，0表示不使用
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;  // 使用7位地址模式
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE; // 禁用双地址模式
    hi2c1.Init.OwnAddress2 = 0;                // 第二地址无效
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; // 禁用通用呼叫模式
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;     // 允许时钟拉伸

    // 调用HAL库函数初始化I2C1，若失败则调用错误处理函数
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief  I2C外设相关GPIO和时钟初始化，HAL库自动调用
  * @param  i2cHandle 指向I2C句柄的指针
  */
void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // 判断是否为I2C1外设
  if(i2cHandle->Instance==I2C1)
  {
    /* 开启GPIOB端口时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /** 配置I2C1的SCL和SDA引脚
      PB6 -> I2C1_SCL
      PB7 -> I2C1_SDA
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;         // 选择PB6和PB7
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;              // 复用开漏模式（I2C必选）
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;        // 设置引脚高速模式
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);              // 初始化GPIOB对应引脚

    /* 使能I2C1时钟 */
    __HAL_RCC_I2C1_CLK_ENABLE();
  }
}

/**
  * @brief  I2C外设GPIO和时钟反初始化，HAL库自动调用
  * @param  i2cHandle 指向I2C句柄的指针
  */
void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{
  // 判断是否为I2C1外设
  if(i2cHandle->Instance==I2C1)
  {
    /* 关闭I2C1时钟 */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /** 反初始化I2C1对应的GPIO引脚 */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);   // 释放PB6引脚
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);   // 释放PB7引脚
  }
}


/******************************** END OF FILE *********************************/
