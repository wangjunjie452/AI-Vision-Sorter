/**
 * @file    serial_parser.c
 * @brief   串口指令解析模块实现
 *
 * @details 工作原理：
 *          1. USART1 每收到 1 字节 → 中断触发 → 存入环形缓冲区
 *          2. 主循环调用 Serial_HasCommand() 检查是否有完整一行（\n 结尾）
 *          3. 有完整行 → Serial_ParseCommand() 解析成 Cmd_t 结构体
 *          4. 解析后传给 Servo_Execute() 执行舵机动作
 *
 * @note    环形缓冲区原理：
 *          ┌───┬───┬───┬───┬───┬───┐
 *          │   │ C │ M │ D │ : │ 0 │  ← head 指向下一个写入位置
 *          └───┴───┴───┴───┴───┴───┘
 *            ↑
 *          tail 指向下一个读取位置
 *          读写都用取模运算，自动循环，不会越界
 */

#include "serial/serial_parser.h"
#include "usart/bsp_usart.h"
#include "led/bsp_led.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ======================= 心跳配置 ======================= */
#define HEARTBEAT_TIMEOUT_MS  10000    /* 10 秒没收到 PING → 判定断连 */

/* ======================= 环形缓冲区 ======================= */
/**
 * @brief   环形缓冲区结构体（static 私有，仅本文件使用）
 * @note    data[]  — 存储区，大小 RX_BUF_SIZE(128)
 *          head    — 写指针，中断每次写入后 +1
 *          tail    — 读指针，主循环每次读取后 +1
 *          lineReady — 收到 '\n' 标志，主循环看到此标志就知道有完整一行
 */
static struct {
    uint8_t  data[RX_BUF_SIZE];
    volatile uint16_t head;      /* 写指针（中断中修改） */
    volatile uint16_t tail;      /* 读指针（主循环修改） */
    volatile uint8_t  lineReady; /* 1 = 有完整一行待解析 */
} rxRing;

/* 临时行缓冲：从环形缓冲区提取一行放到这里再解析 */
static char lineBuf[64];

/* ======================= 心跳相关 ======================= */
static volatile uint32_t lastPingTick = 0;  /* 上次收到 PING 的时间戳 */
static uint8_t heartbeatLost = 0;           /* 1 = 已判定断连（LED 灭） */

/* ======================= 函数实现 ======================= */

/**
 * @brief  初始化串口解析模块
 * @note   清空缓冲区，重置心跳，开启 USART1 中断接收
 */
/* HAL UART 接收缓冲区（非 static，stm32f1xx_it.c 通过 extern 引用） */
uint8_t rx_byte_buf;

void Serial_Init(void)
{
    rxRing.head = 0;
    rxRing.tail = 0;
    rxRing.lineReady = 0;
    lastPingTick = HAL_GetTick();
    heartbeatLost = 0;

    /* 开启 USART1 中断接收，缓冲区必须为全局/静态变量 */
    HAL_UART_Receive_IT(&huart1, &rx_byte_buf, 1);
}

/**
 * @brief  中断回调：将 1 字节存入环形缓冲区
 * @note   由 stm32f1xx_it.c 的 HAL_UART_RxCpltCallback 调用
 *         收到 '\n' 时设置 lineReady = 1，表示一行结束
 */
void Serial_RxByte(uint8_t byte)
{
    uint16_t next = (rxRing.head + 1) % RX_BUF_SIZE;
    if (next == rxRing.tail) return;  /* 缓冲区满了，丢弃这个字节 */
    rxRing.data[rxRing.head] = byte;  /* 存入当前 head 位置 */
    rxRing.head = next;               /* head 前进 */
    if (byte == '\n') rxRing.lineReady = 1;  /* 遇到换行符，标记一行完成 */
}

/**
 * @brief  检查是否有完整指令
 * @note   主循环每轮调用，lineReady == 1 表示有新指令
 */
uint8_t Serial_HasCommand(void)
{
    return rxRing.lineReady;
}

/**
 * @brief  从环形缓冲区提取一行到 lineBuf
 * @note   关中断保护：防止读取过程中 ISR 修改 head 导致数据错乱
 *         读取量极小（<64 字节），关中断时间 < 1μs，不影响实时性
 * @param  buf:    目标缓冲区
 * @param  maxLen: 最大长度
 * @retval 实际读取的字符数
 */
static uint8_t ring_readline(char *buf, uint8_t maxLen)
{
    uint8_t len = 0;
    uint32_t primask = __get_PRIMASK();  /* 保存当前中断状态 */
    __disable_irq();                      /* 关中断（原子操作） */

    while (rxRing.tail != rxRing.head) {
        uint8_t ch = rxRing.data[rxRing.tail];
        rxRing.tail = (rxRing.tail + 1) % RX_BUF_SIZE;
        if (ch == '\n') {
            buf[len] = '\0';
            __set_PRIMASK(primask);       /* 恢复中断状态 */
            return len;
        }
        if (len < maxLen - 1) buf[len++] = ch;
    }

    __set_PRIMASK(primask);               /* 恢复中断状态 */
    buf[len] = '\0';
    return len;
}

/**
 * @brief  解析指令，返回 Cmd_t 结构体
 * @note   支持两种格式：
 *         "PING\n"        → 返回 classId = -99（心跳，不执行动作）
 *         "CMD:0,160,120\n" → 返回 classId=0, x=160, y=120
 *         其他格式         → 返回 classId = -1（无效指令）
 */
Cmd_t Serial_ParseCommand(void)
{
    Cmd_t cmd = { -1, 0, 0 };  /* 默认：无目标 */

    /* 从环形缓冲区提取一行 */
    ring_readline(lineBuf, sizeof(lineBuf));
    rxRing.lineReady = 0;  /* 清除标志，准备接收下一行 */

    /* ---- 情况1：心跳帧 "PING" → 自动回复 "PONG" ---- */
    if (strncmp(lineBuf, "PING", 4) == 0) {
        lastPingTick = HAL_GetTick();  /* 更新心跳时间戳 */
        heartbeatLost = 0;             /* 恢复在线状态 */
        Serial_SendString("PONG\n");   /* 回复 PONG */
        cmd.classId = -99;             /* 特殊标记，主循环会跳过 */
        return cmd;
    }

    /* ---- 情况2：指令帧 "CMD:类ID,X,Y" ---- */
    if (strncmp(lineBuf, "CMD:", 4) != 0) return cmd;  /* 不是 CMD 开头，丢弃 */

    char *p = lineBuf + 4;  /* 跳过 "CMD:" 前缀 */
    char *token;

    /* 用逗号分隔，依次提取 classId、x、y */
    token = strtok(p, ",");
    if (token) cmd.classId = (int8_t)atoi(token);   /* 第一个逗号前 = 类别ID */

    token = strtok(NULL, ",");
    if (token) cmd.x = (int16_t)atoi(token);        /* 第二个 = X 坐标 */

    token = strtok(NULL, ",");
    if (token) cmd.y = (int16_t)atoi(token);        /* 第三个 = Y 坐标 */

    return cmd;
}

/**
 * @brief  发送 ACK 确认帧
 * @note   格式："ACK:0\n"，表示已收到类别 0 的指令
 */
void Serial_SendAck(int8_t classId)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "ACK:%d\n", classId);
    Serial_SendString(buf);
}

/**
 * @brief  发送 READY 完成帧
 * @note   格式："READY\n"，表示舵机动作执行完毕，可接受下一条指令
 */
void Serial_SendReady(void)
{
    Serial_SendString("READY\n");
}

/**
 * @brief  心跳超时检测
 * @note   超过 10 秒没收到 PING → LED 灭，表示与 PC 断开连接
 *         收到 PING 后会自动恢复（LED 重新亮）
 */
void Serial_CheckHeartbeat(void)
{
    if (heartbeatLost) return;  /* 已断连，不重复处理 */
    if (HAL_GetTick() - lastPingTick > HEARTBEAT_TIMEOUT_MS) {
        heartbeatLost = 1;
        LED_OFF();  /* LED 灭 = 断连待机 */
    }
}

/**
 * @brief  底层串口发送（阻塞式）
 * @note   通过 USART1 发送字符串，最长等 100ms
 */
void Serial_SendString(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), 100);
}
