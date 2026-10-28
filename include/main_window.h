/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：main_window.h
 * 模块说明：声明主窗角度闭环、PID状态与限位保护
 * 对应说明书章节：4.5 主窗闭环控制
 */
#ifndef SMART_WINDOW_MAIN_WINDOW_H
#define SMART_WINDOW_MAIN_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 清空增量式 PID 与输出状态。 */
void main_window_init(void);

/*
 * 固定签名：依据目标角与当前角计算 0.0~1.0 PWM，并直接更新电机方向
 * 与驱动输出。pwm_out 可为 NULL；此时仍执行保护和控制。
 */
void main_window_angle_ctrl(float target_angle,
                            float current_angle,
                            float *pwm_out);

/* 固定签名：角度有限且位于机械闭区间内返回 1；越限返回 0 并停机。 */
uint8_t limit_check(float angle);

/* 查询最近一个周期的方向，主要用于状态监控。 */
uint8_t main_window_get_direction(void);

/* 查询最近一个周期参与限幅前的 PID 修正量绝对值。 */
float main_window_get_pid_increment(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_MAIN_WINDOW_H */
