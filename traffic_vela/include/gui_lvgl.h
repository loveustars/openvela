/**
 * @file gui_lvgl.h
 * @brief LVGL 红绿灯可视化 GUI (竖屏 320×480)
 */
#ifndef GUI_LVGL_H
#define GUI_LVGL_H

#include "signal_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

void gui_lvgl_init(void);
void gui_lvgl_update(const SignalDecision *decision, int tick);

/**
 * @brief 边睡边伺候 LVGL (代替 sleep/usleep)
 *
 * 每 5ms 调用一次 lv_timer_handler(), 共持续 ms 毫秒.
 * 参照 lvgldemo 的 while(1) { lv_timer_handler(); usleep(idle); } 模式.
 */
void gui_lvgl_sleep_ms(int ms);

#ifdef __cplusplus
}
#endif

#endif
