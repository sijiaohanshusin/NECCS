#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

#include "arm_math.h"
#include "app_main_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 显示模块初始化（可重复调用，内部会重置状态）。 */
void App_Display_Init(void);

/* 渲染一帧 UI：热力图 + 十字光标 + 诊断信息。 */
void App_Display_Render(const Sound_Pos_t *pos, const float32_t *coarse_power, uint32_t frame_seq);

/* 返回显示模块是否初始化成功。 */
uint8_t App_Display_IsReady(void);

/* 初始化阶段和错误码（用于串口/调试器定位问题）。 */
extern volatile uint32_t g_display_init_stage;
extern volatile uint32_t g_display_init_error;

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
