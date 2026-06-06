#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#include "main.h"

/* ------------------------- LED引脚定义 ------------------------- */
/* 蓝色药丸板载 LED：PC13，低电平点亮 */
#define LED_Pin     GPIO_PIN_13
#define LED_Port    GPIOC

/* ------------------------- 函数声明 ------------------------- */
void LED_GPIO_Config(void);

/* ------------------------- LED控制宏（低电平亮） ------------------------- */
#define LED_ON()       HAL_GPIO_WritePin(LED_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_OFF()      HAL_GPIO_WritePin(LED_Port, LED_Pin, GPIO_PIN_SET)
#define LED_TOGGLE()   HAL_GPIO_TogglePin(LED_Port, LED_Pin)

#endif /* __BSP_LED_H__ */

