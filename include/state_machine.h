/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：state_machine.h
 * 模块说明：声明六状态控制机、迟滞迁移与安全锁复位
 * 对应说明书章节：4.4 工作状态机
 */
#ifndef SMART_WINDOW_STATE_MACHINE_H
#define SMART_WINDOW_STATE_MACHINE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 固定签名：调用决策器产生候选动作，再依据上一状态与安全锁存状态完成
 * 正式迁移。安全、雨天和手动分支即时迁移；普通自动分支需稳定确认。
 */
void state_machine_run(const env_data_t *env,
                       const manual_cmd_t *manual,
                       const safety_status_t *safe,
                       window_cmd_t *cmd,
                       system_state_t *state);

/* 清空迁移确认计时与安全锁存，供初始化、故障恢复和主机测试使用。 */
void state_machine_reset(void);

/* 查询安全锁是否仍处于粘滞状态，0 表示已经允许正常自动控制。 */
uint8_t state_machine_safe_is_latched(void);

/* 把状态枚举转换为稳定英文名称，未知值返回 "UNKNOWN"。 */
const char *state_machine_name(system_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_STATE_MACHINE_H */
