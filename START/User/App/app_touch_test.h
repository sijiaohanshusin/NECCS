/**
 * @file   app_touch_test.h
 * @brief  触摸测试页面接口
 * @details 提供触摸屏校验 / 测试绘制函数，将触摸坐标与压力信息实时渲染到 LCD。
 */
#ifndef APP_TOUCH_TEST_H
#define APP_TOUCH_TEST_H

/** @brief 渲染触摸测试画面，在屏幕上显示当前触点位置及轨迹 */
void App_TouchTest_Render(void);

#endif /* APP_TOUCH_TEST_H */
