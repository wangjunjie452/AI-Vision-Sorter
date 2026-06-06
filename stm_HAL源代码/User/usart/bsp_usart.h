#ifndef __BSP_USART_H__
#define __BSP_USART_H__

#include "main.h"

extern UART_HandleTypeDef huart1;   // 外部声明 USART1 句柄

/************ 函数声明 ************/
void Debug_UART_Init(void);

#endif /* __BSP_USART_H__ */
