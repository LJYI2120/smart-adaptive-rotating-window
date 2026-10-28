/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：app.h
 * 模块说明：声明应用初始化、调度和主机诊断公共接口
 * 对应说明书章节：3.3 软件运行架构
 */
#ifndef SMART_WINDOW_APP_H
#define SMART_WINDOW_APP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化所有静态模块和安全默认命令。 */
void app_init(void);

/* 裸机主循环入口：安全覆盖、模式裁决、副窗决策和状态上传。 */
void app_run(void);

/*
 * 周期控制回调。目标板由硬件定时器以 SAMPLE_PERIOD_MS 触发，完成采样、
 * 滤波、主窗角度闭环和 PWM 更新。
 */
void app_timer_tick(void);

/* 线程安全地安装一条由协议解析器生成的手动命令；NULL 表示清空。 */
void app_set_manual_command(const manual_cmd_t *manual);

/*
 * 只读诊断接口，主要用于主机测试、调试器和低优先级日志任务。
 * 返回值指向模块静态对象，调用者不得修改或跨初始化周期保存。
 */
uint32_t app_get_timer_tick_count(void);
const env_data_t *app_get_filtered_environment(void);
const safety_status_t *app_get_safety_status(void);
const window_cmd_t *app_get_command(void);
system_state_t app_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_APP_H */
