/**
 * @file    app_task_cfg.h
 * @brief   FreeRTOS 任务调度配置 — 兼容入口头文件
 * @details
 * 历史背景：
 *   早期版本中该文件直接定义了任务优先级和堆栈大小等宏。
 *   为了实现"单一可配置入口"的设计目标，所有可调参数已统一迁移至
 *   `app_user_config.h`，本文件现在仅作为向下兼容的"转发头"存在。
 *
 * 使用说明：
 *   若已有模块包含了 `app_task_cfg.h`，无需修改，效果等同于包含
 *   `app_user_config.h`。新增模块建议直接包含 `app_user_config.h`。
 *
 * [改进] 未来可以在合适时机移除此文件，统一使用 app_user_config.h，
 *        避免新人误认为配置项分散在两个文件中。
 *
 * 依赖关系：
 *   本文件 → app_user_config.h（唯一实际内容来源）
 *
 * 可配置项说明（均在 app_user_config.h 中）：
 *   - APP_AUDIO_TASK_PRIO       音频任务优先级
 *   - APP_UI_TASK_PRIO          UI 任务优先级
 *   - APP_AUDIO_TASK_STACK_WORDS 音频任务堆栈大小（单位：字）
 *   - APP_UI_TASK_STACK_WORDS   UI 任务堆栈大小（单位：字）
 */
#ifndef APP_TASK_CFG_H           /* 头文件防重复包含保护（开始） */
#define APP_TASK_CFG_H           /* 定义本文件标识宏，防止多次 include */

/* 将所有实际配置内容的职责转发给统一配置入口文件 */
/* [注意] 此处只能有一个 include，避免污染全局命名空间 */
#include "app_user_config.h"     /* 统一可配置项入口：任务栈/优先级/算法/显示参数 */

#endif /* APP_TASK_CFG_H */      /* 头文件防重复包含保护（结束） */
