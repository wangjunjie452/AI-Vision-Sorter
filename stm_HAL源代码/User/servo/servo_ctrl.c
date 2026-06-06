/**
 * @file    servo_ctrl.c
 * @brief   舵机控制模块实现（PCA9685 驱动 + 非阻塞状态机）
 *
 * @details 动作流程：
 *          A 类：CH1 先让开(0°) → CH0 放行(0°) → 物品落入 A 箱 → 全部复位
 *          B 类：CH0 放行(0°) → CH0 阻挡(90°) → 等物品到达 CH1 → CH1 左转(0°) → 落入 B 箱 → 复位
 *          C 类：CH0 放行(0°) → CH0 阻挡(90°) → 等物品到达 CH1 → CH1 右转(180°) → 落入 C 箱 → 复位
 */

#include "servo/servo_ctrl.h"
#include "serial/serial_parser.h"
#include "usart/bsp_usart.h"
#include "i2c/bsp_i2c.h"
#include <stdio.h>

/* ==================== PCA9685 I2C 底层 ==================== */

static uint8_t PCA9685_WriteReg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };
    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(&hi2c1, PCA9685_ADDR << 1, data, 2, 100);
    return (ret == HAL_OK) ? 1 : 0;
}

static uint8_t PCA9685_ReadReg(uint8_t reg, uint8_t *val)
{
    HAL_StatusTypeDef ret;
    ret = HAL_I2C_Master_Transmit(&hi2c1, PCA9685_ADDR << 1, &reg, 1, 100);
    if (ret != HAL_OK) return 0;
    ret = HAL_I2C_Master_Receive(&hi2c1, PCA9685_ADDR << 1, val, 1, 100);
    return (ret == HAL_OK) ? 1 : 0;
}

static uint8_t PCA9685_SetPWM(uint8_t channel, uint16_t on, uint16_t off)
{
    uint8_t reg = PCA9685_LED0_ON_L + 4 * channel;
    uint8_t data[5] = {
        reg,
        (uint8_t)(on & 0xFF),
        (uint8_t)((on >> 8) & 0x0F),
        (uint8_t)(off & 0xFF),
        (uint8_t)((off >> 8) & 0x0F)
    };
    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(&hi2c1, PCA9685_ADDR << 1, data, 5, 100);
    return (ret == HAL_OK) ? 1 : 0;
}

static uint8_t PCA9685_SetFreq(float freq)
{
    float prescale = 25000000.0f / (4096.0f * freq) - 1.0f;
    char dbg[48];

    uint8_t ok;
    ok = PCA9685_WriteReg(PCA9685_MODE1, 0x10);   /* 进入 Sleep */
    snprintf(dbg, sizeof(dbg), "  [PCA] Sleep write: %s\n", ok ? "OK" : "FAIL");
    Serial_SendString(dbg);

    ok = PCA9685_WriteReg(PCA9685_PRESCALE, (uint8_t)prescale);  /* 设置分频 */
    snprintf(dbg, sizeof(dbg), "  [PCA] Prescale=%d write: %s\n", (uint8_t)prescale, ok ? "OK" : "FAIL");
    Serial_SendString(dbg);

    HAL_Delay(5);

    ok = PCA9685_WriteReg(PCA9685_MODE1, 0xA0);   /* Restart + Auto-Increment */
    snprintf(dbg, sizeof(dbg), "  [PCA] MODE1=0xA0 write: %s\n", ok ? "OK" : "FAIL");
    Serial_SendString(dbg);

    /* 读回验证 */
    uint8_t mode1 = 0, prescale_rb = 0;
    PCA9685_ReadReg(PCA9685_MODE1, &mode1);
    PCA9685_ReadReg(PCA9685_PRESCALE, &prescale_rb);
    snprintf(dbg, sizeof(dbg), "  [PCA] 读回: MODE1=0x%02X PRESCALE=%d\n", mode1, prescale_rb);
    Serial_SendString(dbg);

    return (mode1 == 0xA0) ? 1 : 0;
}

/* ==================== 状态机 ==================== */

typedef struct {
    uint8_t  channel;
    uint16_t angle;
    uint16_t hold_ms;
} ServoStep_t;

static struct {
    const ServoStep_t *steps;
    uint8_t       step_count;
    uint8_t       step_idx;
    uint32_t      step_start_tick;
    uint8_t       busy;
    uint8_t       step_angle_sent;  /* 当前步骤的角度是否已发送 */
} sm = { NULL, 0, 0, 0, 0, 0 };

/* ==================== 动作序列 ==================== */

/* A 类：CH1 先让开 → CH0 放行掉入 A → CH0 挡回 → 等掉落 → CH1 复位 */
static const ServoStep_t seq_class_a[] = {
    { SERVO_CH_DIVERT, ANGLE_DIVERT_LEFT,  300 },                 /* CH1 先转开让路 */
    { SERVO_CH_GATE,   ANGLE_GATE_PASS,    DELAY_GATE_OPEN_MS },  /* CH0 放行，物品掉入 A 箱 */
    { SERVO_CH_GATE,   ANGLE_GATE_BLOCK,   1000 },                /* CH0 挡回，等物品完全掉落 */
    { SERVO_CH_DIVERT, ANGLE_DIVERT_CENTER, 0 },                  /* CH1 复位，完成 */
};

/* B 类：CH0 放行 → CH0 阻挡 → 等物品到达 → CH1 左转 → 落入 B 箱 → 复位 */
static const ServoStep_t seq_class_b[] = {
    { SERVO_CH_GATE,   ANGLE_GATE_PASS,     300 },                  /* CH0 放行，让物品进入传送带 */
    { SERVO_CH_GATE,   ANGLE_GATE_BLOCK,    DELAY_ITEM_ARRIVE_MS }, /* CH0 阻挡，等物品传送到 CH1 */
    { SERVO_CH_DIVERT, ANGLE_DIVERT_LEFT,   1000 },                 /* CH1 左转，物品落入 B 箱 */
    { SERVO_CH_DIVERT, ANGLE_DIVERT_CENTER, 0 },                    /* CH1 复位，完成 */
};

/* C 类：CH0 放行 → CH0 阻挡 → 等物品到达 → CH1 右转 → 落入 C 箱 → 复位 */
static const ServoStep_t seq_class_c[] = {
    { SERVO_CH_GATE,   ANGLE_GATE_PASS,      300 },                  /* CH0 放行，让物品进入传送带 */
    { SERVO_CH_GATE,   ANGLE_GATE_BLOCK,     DELAY_ITEM_ARRIVE_MS }, /* CH0 阻挡，等物品传送到 CH1 */
    { SERVO_CH_DIVERT, ANGLE_DIVERT_RIGHT,   1000 },                 /* CH1 右转，物品落入 C 箱 */
    { SERVO_CH_DIVERT, ANGLE_DIVERT_CENTER,  0 },                    /* CH1 复位，完成 */
};

/* ==================== 接口实现 ==================== */

void Servo_Init(void)
{
    char dbg[64];

    /* 1. 探测 PCA9685 是否在线 */
    HAL_StatusTypeDef probe = HAL_I2C_IsDeviceReady(&hi2c1, PCA9685_ADDR << 1, 3, 200);
    snprintf(dbg, sizeof(dbg), "[INIT] PCA9685 探测: %s (addr=0x%02X)\n",
             (probe == HAL_OK) ? "ONLINE" : "OFFLINE", PCA9685_ADDR);
    Serial_SendString(dbg);

    /* 2. 设置 50Hz */
    uint8_t freq_ok = PCA9685_SetFreq(50.0f);
    snprintf(dbg, sizeof(dbg), "[INIT] SetFreq(50Hz): %s\n", freq_ok ? "OK" : "FAIL");
    Serial_SendString(dbg);

    /* 3. 设初始角度 */
    uint8_t r1 = Servo_SetAngle(SERVO_CH_GATE, ANGLE_GATE_BLOCK);
    uint8_t r2 = Servo_SetAngle(SERVO_CH_DIVERT, ANGLE_DIVERT_CENTER);
    snprintf(dbg, sizeof(dbg), "[INIT] CH0->%d°: %s, CH1->%d°: %s\n",
             ANGLE_GATE_BLOCK, r1 ? "OK" : "FAIL",
             ANGLE_DIVERT_CENTER, r2 ? "OK" : "FAIL");
    Serial_SendString(dbg);

    sm.busy = 0;
    Serial_SendString("[INIT] Servo_Init 完成\n");
}

uint8_t Servo_SetAngle(uint8_t channel, uint16_t angle)
{
    if (angle > 180) angle = 180;
    uint16_t pulse = SERVO_MIN + (SERVO_MAX - SERVO_MIN) * angle / 180;
    uint8_t ok = PCA9685_SetPWM(channel, 0, pulse);

    char dbg[48];
    snprintf(dbg, sizeof(dbg), "  [PWM] CH%d angle=%3d pulse=%3d %s\n",
             channel, angle, pulse, ok ? "OK" : "FAIL");
    Serial_SendString(dbg);

    return ok;
}

void Servo_Execute(Cmd_t cmd)
{
    if (sm.busy) {
        Serial_SendString("[EXEC] 忙碌中，忽略新指令\n");
        return;
    }

    char dbg[48];

    switch (cmd.classId) {
        case 0:
            sm.steps = seq_class_a;
            sm.step_count = sizeof(seq_class_a) / sizeof(seq_class_a[0]);
            Serial_SendString("[EXEC] → A类序列\n");
            break;
        case 1:
            sm.steps = seq_class_b;
            sm.step_count = sizeof(seq_class_b) / sizeof(seq_class_b[0]);
            Serial_SendString("[EXEC] → B类序列\n");
            break;
        case 2:
            sm.steps = seq_class_c;
            sm.step_count = sizeof(seq_class_c) / sizeof(seq_class_c[0]);
            Serial_SendString("[EXEC] → C类序列\n");
            break;
        case -1:
            Serial_SendString("[EXEC] → 归位(-1)\n");
            Servo_SetAngle(SERVO_CH_GATE, ANGLE_GATE_BLOCK);
            Servo_SetAngle(SERVO_CH_DIVERT, ANGLE_DIVERT_CENTER);
            return;
        case -2:
            Serial_SendString("[EXEC] → 急停(-2)\n");
            Servo_StopAll();
            return;
        default:
            snprintf(dbg, sizeof(dbg), "[EXEC] → 未知classId=%d，忽略\n", cmd.classId);
            Serial_SendString(dbg);
            return;
    }

    sm.step_idx = 0;
    sm.busy = 1;
    sm.step_angle_sent = 1;  /* step 0 已在下方执行，避免 Servo_Update 重复写入 */
    sm.step_start_tick = HAL_GetTick();

    snprintf(dbg, sizeof(dbg), "[EXEC] 第0步: CH%d→%d° hold=%dms\n",
             sm.steps[0].channel, sm.steps[0].angle, sm.steps[0].hold_ms);
    Serial_SendString(dbg);

    Servo_SetAngle(sm.steps[0].channel, sm.steps[0].angle);
}

uint8_t Servo_Update(void)
{
    if (!sm.busy) return 0;

    const ServoStep_t *cur = &sm.steps[sm.step_idx];

    /* 当前步骤尚未执行：设置角度 */
    if (!sm.step_angle_sent) {
        char dbg[48];
        snprintf(dbg, sizeof(dbg), "[SM] 步骤%d: CH%d→%d° hold=%dms\n",
                 sm.step_idx, cur->channel, cur->angle, cur->hold_ms);
        Serial_SendString(dbg);
        Servo_SetAngle(cur->channel, cur->angle);
        sm.step_angle_sent = 1;
        sm.step_start_tick = HAL_GetTick();

        /* hold_ms=0：执行完立即完成 */
        if (cur->hold_ms == 0) {
            Serial_SendString("[SM] hold=0，完成\n");
            sm.busy = 0;
            return 1;
        }
        return 0;
    }

    /* 当前步骤已执行，等待 hold_ms 时间到 */
    if (HAL_GetTick() - sm.step_start_tick < cur->hold_ms) {
        return 0;
    }

    /* 进入下一步 */
    sm.step_idx++;
    if (sm.step_idx >= sm.step_count) {
        Serial_SendString("[SM] 所有步骤完成\n");
        sm.busy = 0;
        return 1;
    }

    sm.step_angle_sent = 0;
    return 0;
}

uint8_t Servo_IsIdle(void)
{
    return sm.busy ? 0 : 1;
}

void Servo_StopAll(void)
{
    sm.busy = 0;
    for (uint8_t ch = 0; ch < 16; ch++) {
        PCA9685_SetPWM(ch, 0, 0);
    }
}

/**
 * @brief  舵机校准扫描（上电后调用，验证舵机行程是否正常）
 *
 *         CH0（入口挡板，90°舵机）：0°放行 → 90°阻挡 → 复位
 *         CH1（末端分拣，180°舵机）：0°左 → 180°右 → 90°中 → 复位
 */
void Servo_Calibrate(uint16_t pause_ms)
{
    char dbg[48];

    Serial_SendString("\n=== 开始校准扫描 ===\n");

    /* ---------- CH0 入口挡板（90°舵机）：0° → 90° → 复位 ---------- */
    snprintf(dbg, sizeof(dbg), "CAL: CH0 -> %d deg (放行)\n", ANGLE_GATE_PASS);
    Serial_SendString(dbg);
    Servo_SetAngle(SERVO_CH_GATE, ANGLE_GATE_PASS);
    HAL_Delay(pause_ms);

    snprintf(dbg, sizeof(dbg), "CAL: CH0 -> %d deg (阻挡)\n", ANGLE_GATE_BLOCK);
    Serial_SendString(dbg);
    Servo_SetAngle(SERVO_CH_GATE, ANGLE_GATE_BLOCK);
    HAL_Delay(pause_ms);

    Servo_SetAngle(SERVO_CH_GATE, ANGLE_GATE_BLOCK);
    Serial_SendString("CAL: CH0 -> 复位\n");
    HAL_Delay(500);

    /* ---------- CH1 末端分拣（180°舵机）：左 → 右 → 中 → 复位 ---------- */
    snprintf(dbg, sizeof(dbg), "CAL: CH1 -> %d deg (left/左)\n", ANGLE_DIVERT_LEFT);
    Serial_SendString(dbg);
    Servo_SetAngle(SERVO_CH_DIVERT, ANGLE_DIVERT_LEFT);
    HAL_Delay(pause_ms);

    snprintf(dbg, sizeof(dbg), "CAL: CH1 -> %d deg (right/右)\n", ANGLE_DIVERT_RIGHT);
    Serial_SendString(dbg);
    Servo_SetAngle(SERVO_CH_DIVERT, ANGLE_DIVERT_RIGHT);
    HAL_Delay(pause_ms);

    snprintf(dbg, sizeof(dbg), "CAL: CH1 -> %d deg (center/中)\n", ANGLE_DIVERT_CENTER);
    Serial_SendString(dbg);
    Servo_SetAngle(SERVO_CH_DIVERT, ANGLE_DIVERT_CENTER);
    HAL_Delay(pause_ms);

    Serial_SendString("CAL: CH1 -> 复位\n");
    HAL_Delay(500);

    Serial_SendString("=== 校准扫描完成 ===\n");
}
