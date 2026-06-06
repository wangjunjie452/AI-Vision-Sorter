#include "buzzer/bsp_buzzer.h"

/**
  * @brief  初始化蜂鸣器 GPIO（PA1，推挽输出，默认低电平）
  */
void Buzzer_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin   = BUZZER_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_Port, &GPIO_InitStruct);

    BUZZER_OFF();
}

/**
  * @brief  短促响一声
  */
void Buzzer_Beep(uint16_t ms)
{
    BUZZER_ON();
    HAL_Delay(ms);
    BUZZER_OFF();
}
