/**
 * @file    serial_parser.h
 * @brief   串口指令解析模块 — 接收并解析 PC/K210 发来的分拣指令
 *
 * @details 通信协议：
 *          ┌─────────────────────────────────────────────────┐
 *          │ 指令帧（PC → STM32）：  "CMD:类别ID,X,Y\n"     │
 *          │ 心跳帧（PC → STM32）：  "PING\n"               │
 *          │ 心跳响应（STM32 → PC）： "PONG\n"              │
 *          │ 确认帧（STM32 → PC）：  "ACK:类别ID\n"         │
 *          │ 完成帧（STM32 → PC）：  "READY\n"              │
 *          └─────────────────────────────────────────────────┘
 *
 * @note    使用环形缓冲区存储串口数据，中断每收一个字节就存入，
 *          主循环检查是否有完整的一行（以 '\n' 结尾），有则解析。
 *
 * @note    类别 ID 含义：
 *          0 = 类型A物体（放行 → 转臂向左 → A盒）
 *          1 = 类型B物体（挡板向上 → B盒）
 *          2 = 类型C物体（放行 → 转臂向右 → C盒）
 *         -1 = 无目标（舵机归位）
 *         -2 = 急停（立即停止所有舵机）
 *        -99 = 内部标记（心跳 PING，不执行动作）
 */

#ifndef __SERIAL_PARSER_H
#define __SERIAL_PARSER_H

#include "main.h"

/* ======================= 环形缓冲区配置 ======================= */
#define RX_BUF_SIZE  128    /* 接收缓冲区大小（字节），一般 128 够用 */

/* ======================= 指令结构体 ======================= */
/**
 * @brief   解析后的指令结构体
 * @note    由 Serial_ParseCommand() 返回，传给 Servo_Execute() 执行
 */
typedef struct {
    int8_t   classId;     /* 物体类别 ID：0(A), 1(B), 2(C), -1(无目标), -2(急停) */
    int16_t  x;           /* 检测框中心 X 坐标（像素，当前未使用，预留） */
    int16_t  y;           /* 检测框中心 Y 坐标（像素，当前未使用，预留） */
} Cmd_t;

/* ======================= 函数声明 ======================= */

/**
 * @brief  初始化串口解析模块
 * @note   清空环形缓冲区，开启 USART1 中断接收（每字节触发一次）
 * @param  无
 * @retval 无
 */
void Serial_Init(void);

/**
 * @brief  将中断收到的 1 字节存入环形缓冲区
 * @note   由 stm32f1xx_it.c 中的 HAL_UART_RxCpltCallback 调用
 * @param  byte: 中断收到的字节
 * @retval 无
 */
void Serial_RxByte(uint8_t byte);

/**
 * @brief  检查是否收到一条完整指令（以 '\n' 结尾）
 * @note   主循环每轮调用，返回 1 表示有新指令可解析
 * @param  无
 * @retval 1 = 有完整指令，0 = 没有
 */
uint8_t Serial_HasCommand(void);

/**
 * @brief  解析环形缓冲区中的指令，返回 Cmd_t 结构体
 * @note   调用后 lineReady 自动清零
 *         如果是 PING 心跳，返回 classId = -99（不执行动作）
 *         如果格式错误，返回 classId = -1（无目标）
 * @param  无
 * @retval 解析后的指令结构体
 */
Cmd_t Serial_ParseCommand(void);

/**
 * @brief  发送 ACK 确认帧给 PC
 * @note   格式："ACK:类别ID\n"，表示已收到指令并开始执行
 * @param  classId: 收到的类别 ID
 * @retval 无
 */
void Serial_SendAck(int8_t classId);

/**
 * @brief  发送 READY 完成帧给 PC
 * @note   格式："READY\n"，表示舵机动作已执行完毕
 * @param  无
 * @retval 无
 */
void Serial_SendReady(void);

/**
 * @brief  底层串口发送函数（阻塞式）
 * @note   通过 USART1 发送字符串，超时 100ms
 * @param  str: 要发送的字符串（以 '\0' 结尾）
 * @retval 无
 */
void Serial_SendString(const char *str);

/**
 * @brief  心跳超时检测
 * @note   主循环每轮调用，10 秒没收到 PING → LED 灭（进入待机模式）
 * @param  无
 * @retval 无
 */
void Serial_CheckHeartbeat(void);

#endif /* __SERIAL_PARSER_H */
