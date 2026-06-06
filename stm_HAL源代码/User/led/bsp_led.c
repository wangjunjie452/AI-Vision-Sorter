#include "led/bsp_led.h"

/**
  * @brief  初始化 PC13 板载 LED（推挽输出，默认灭）
  */
void LED_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin   = LED_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_Port, &GPIO_InitStruct);

    /* 默认灭（高电平） */
    HAL_GPIO_WritePin(LED_Port, LED_Pin, GPIO_PIN_SET);
}

