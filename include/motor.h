/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：motor.h
 * 模块说明：声明主副窗电机驱动、方向、反馈和堵转保护接口
 * 对应说明书章节：4.6 电机执行层
 */
#ifndef SMART_WINDOW_MOTOR_H
#define SMART_WINDOW_MOTOR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MOTOR_DIR_STOP = 0,
  MOTOR_DIR_FORWARD,
  MOTOR_DIR_REVERSE
} motor_direction_t;

/* 初始化电机方向脚、PWM与内部故障状态。 */
void motor_init(void);

/* 绑定安全状态对象；绑定对象生命周期必须长于电机模块使用期。 */
void motor_bind_safety_status(safety_status_t *safe);

/* 读取主窗角度，编码器计数按配置换算为度。 */
float motor_read_main_angle_deg(void);

/* 读取当前电机电流，单位毫安；异常采样返回 0。 */
int motor_read_current_ma(void);

/*
 * 原子式更新主窗驱动：先写入方向，再写 PWM。零占空比或无效值都会
 * 停止主窗电机，方向变化前短时关断由 HAL/驱动板负责死区。
 */
void motor_set_main_output(float duty, motor_direction_t direction);

/* 设置副窗方向并以固定低速占空比启动；STOP 会关闭副窗 PWM。 */
void motor_set_sub_direction(motor_direction_t direction);

/* 停止全部电机并清除方向输出，不自动清除历史堵转标志。 */
void motor_stop_all(void);

/* 固定签名：检测当前运动电流，超过 STALL_CURRENT_MA 时停机并置故障。 */
void motor_stall_protect(void);

/* 当副窗速度过低但有驱动命令时，也可由行程服务判定为堵转。 */
void motor_mark_stall(void);

uint8_t motor_has_stall(void);
uint8_t motor_is_drive_active(void);
motor_direction_t motor_get_sub_direction(void);

/* 人工复位时清除粘滞堵转，仅应在危险条件消失后调用。 */
void motor_clear_stall(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_MOTOR_H */
