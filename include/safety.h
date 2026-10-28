/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：safety.h
 * 模块说明：声明安全覆盖、故障粘滞、通信超时和手动复位接口
 * 对应说明书章节：4.7 安全覆盖与异常处理
 */
#ifndef SMART_WINDOW_SAFETY_H
#define SMART_WINDOW_SAFETY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 清空安全状态，不改变已启动的执行器。 */
void safety_init(safety_status_t *safe);

/*
 * 固定签名：根据当前环境、传感器有效位和通信时间更新安全覆盖。
 * 风/雨/传感器/通信可随恢复条件自动解除，电机堵转保留至复位或超时。
 */
void safety_override(const env_data_t *env,
                     safety_status_t *safe);

/* 通信解析层登记一次人工复位请求，下一次主循环消费。 */
void safety_request_manual_reset(void);

/* 原子式读取并清除复位请求，返回非零表示本周期应执行复位。 */
uint8_t safety_consume_manual_reset(void);

/* 清除风、雨、传感器、通信的人工可复位锁存。 */
void safety_clear_latches(safety_status_t *safe);

/* 任意安全锁非零时返回 1。 */
uint8_t safety_any_lock(const safety_status_t *safe);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_SAFETY_H */
