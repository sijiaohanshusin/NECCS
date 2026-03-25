#ifndef TOUCH_GT9XXX_H
#define TOUCH_GT9XXX_H

#include "touch.h"

#define TOUCH_GT9XXX_RST_GPIO_PORT GPIOB
#define TOUCH_GT9XXX_RST_GPIO_PIN  GPIO_PIN_14

#define TOUCH_GT9XXX_INT_GPIO_PORT GPIOH
#define TOUCH_GT9XXX_INT_GPIO_PIN  GPIO_PIN_7

#define TOUCH_GT9XXX_CMD_WR 0x28u
#define TOUCH_GT9XXX_CMD_RD 0x29u

#define TOUCH_GT9XXX_CTRL_REG  0x8040u
#define TOUCH_GT9XXX_PID_REG   0x8140u
#define TOUCH_GT9XXX_GSTID_REG 0x814Eu
#define TOUCH_GT9XXX_TP1_REG   0x8150u

uint8_t Touch_GT9XXX_Init(void);
uint8_t Touch_GT9XXX_Scan(Touch_State_t *state);
uint8_t Touch_GT9XXX_GetMaxPoints(void);
uint8_t Touch_GT9XXX_DebugStatus(void);
uint8_t Touch_GT9XXX_DebugPointNum(void);
uint8_t Touch_GT9XXX_DebugValidCount(void);
uint16_t Touch_GT9XXX_DebugRawX0(void);
uint16_t Touch_GT9XXX_DebugRawY0(void);
uint16_t Touch_GT9XXX_DebugMapX0(void);
uint16_t Touch_GT9XXX_DebugMapY0(void);
uint32_t Touch_GT9XXX_DebugScanCount(void);

#endif /* TOUCH_GT9XXX_H */
