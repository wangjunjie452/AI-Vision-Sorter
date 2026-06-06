#ifndef __BSP_BUZZER_H__
#define __BSP_BUZZER_H__

#include "main.h"

/* 蜂鸣器引脚：PA1（高电平响） */
#define BUZZER_Pin    GPIO_PIN_1
#define BUZZER_Port   GPIOA

void Buzzer_GPIO_Config(void);

#define BUZZER_ON()      HAL_GPIO_WritePin(BUZZER_Port, BUZZER_Pin, GPIO_PIN_SET)
#define BUZZER_OFF()     HAL_GPIO_WritePin(BUZZER_Port, BUZZER_Pin, GPIO_PIN_RESET)
#define BUZZER_TOGGLE()  HAL_GPIO_TogglePin(BUZZER_Port, BUZZER_Pin)

/* 短促响一声（阻塞 ms 毫秒） */
void Buzzer_Beep(uint16_t ms);

#endif /* __BSP_BUZZER_H__ */
