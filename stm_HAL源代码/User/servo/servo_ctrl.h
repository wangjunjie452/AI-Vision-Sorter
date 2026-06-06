/**
 * @file    servo_ctrl.h
 * @brief   舵机控制模块（PCA9685 驱动 + 非阻塞状态机）
 *
 * @details 传送带分拣方案（双舵机 + 纸板机构）：
 *          CH0 — 入口挡板（90° 舵机，只用 0° 和 90°）
 *            90° = 阻挡（默认，纸板竖直挡住物品，等待分拣判断）
 *            0°  = 放行（纸板倒下，物品直接落入 A 箱）
 *
 *          CH1 — 末端落料分拣（180° 舵机）
 *            90° = 居中（默认，纸板挡住物品）
 *            0°  = 向左打满（格挡转出，物品掉入 B 箱）
 *            180°= 向右打满（格挡转出，物品掉入 C 箱）
 *
 *          分拣逻辑：
 *            A 类：CH1 先让开(0°) → CH0 放行(0°) → 物品直接落入 A 箱 → 全部复位
 *            B 类：CH0 放行(0°) → CH0 阻挡(90°) → 物品到达 CH1 → CH1 左转 → 落入 B 箱 → 复位
 *            C 类：CH0 放行(0°) → CH0 阻挡(90°) → 物品到达 CH1 → CH1 右转 → 落入 C 箱 → 复位
 *
 *          预留红外传感器接口：DELAY_ITEM_ARRIVE_MS 未来改为红外检测触发
 */
#ifndef __SERVO_CTRL_H
#define __SERVO_CTRL_H

#include "main.h"
#include "serial/serial_parser.h"
#include <stdint.h>

/* ==================== PCA9685 配置 ==================== */
#define PCA9685_ADDR      0x40
#define PCA9685_MODE1      0x00
#define PCA9685_PRESCALE   0xFE
#define PCA9685_LED0_ON_L  0x06

/* SG90 舵机 PWM 范围（校准值）
 * PCA9685 @50Hz，周期 20ms，分辨率 4096
 * SG90 标准：0.5ms(0°) ~ 2.5ms(180°)
 * 计算：0.5/20*4096=102, 2.5/20*4096=512
 * 预留余量：110 ~ 500（防止卡到机械限位）
 */
#define SERVO_MIN   110    /* 0° → 约 0.54ms */
#define SERVO_MAX   500    /* 180° → 约 2.44ms */

/* ==================== 舵机通道 ==================== */
#define SERVO_CH_GATE    0   /* CH0: 入口挡板（90° 舵机） */
#define SERVO_CH_DIVERT  1   /* CH1: 末端落料分拣（180° 舵机） */

/* ==================== 角度定义 ==================== */

/* CH0 入口挡板（90° 舵机，只用 0° 和 90°） */
#define ANGLE_GATE_BLOCK   90   /* 阻挡（默认位置，纸板竖直挡住物品） */
#define ANGLE_GATE_PASS    0    /* 放行（纸板倒下，物品落入 A 箱） */

/* CH1 末端落料分拣（180° 舵机） */
#define ANGLE_DIVERT_LEFT  0    /* 向左打满 → B 箱 */
#define ANGLE_DIVERT_CENTER 90  /* 居中（默认，挡住物品） */
#define ANGLE_DIVERT_RIGHT 180  /* 向右打满 → C 箱 */

/* ==================== 等待时间（预留红外接口） ==================== */
#define DELAY_GATE_OPEN_MS      800   /* A 类：CH0 放行后等待物品掉落（越短误判窗口越小） */
#define DELAY_ITEM_ARRIVE_MS    2000  /* B/C 类：物品到达 CH1 等待（未来改红外检测） */

/* ==================== 函数接口 ==================== */
void Servo_Init(void);
uint8_t Servo_SetAngle(uint8_t channel, uint16_t angle);
void Servo_Execute(Cmd_t cmd);
uint8_t Servo_Update(void);
uint8_t Servo_IsIdle(void);
void Servo_StopAll(void);
void Servo_Calibrate(uint16_t pause_ms);

#endif /* __SERVO_CTRL_H */
