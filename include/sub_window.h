/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：sub_window.h
 * 模块说明：声明副窗非阻塞开关、行程超时与堵转联动
 * 对应说明书章节：4.6 副窗执行控制
 */
#ifndef SMART_WINDOW_SUB_WINDOW_H
#define SMART_WINDOW_SUB_WINDOW_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SUB_WINDOW_CLOSED = 0,
  SUB_WINDOW_OPENING,
  SUB_WINDOW_OPEN,
  SUB_WINDOW_CLOSING,
  SUB_WINDOW_FAULT
} sub_window_state_t;

/* 读取限位并建立确定的初始位置。 */
void sub_window_init(void);

/* 固定签名：请求打开副窗。已打开时不重复启动。 */
void sub_window_open(void);

/* 固定签名：请求关闭副窗。已关闭时不重复启动。 */
void sub_window_close(void);

/* 由主循环或周期服务调用，推进限位、超时和堵转判断。 */
void sub_window_service(void);

/* 绑定安全对象，使行程故障能够直接写入 safety_status_t.motor_stall。 */
void sub_window_bind_safety_status(safety_status_t *safe);

sub_window_state_t sub_window_get_state(void);
uint8_t sub_window_is_open(void);
uint8_t sub_window_is_moving(void);

/* 人工复位后清除执行故障，并重新读取限位。 */
void sub_window_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_SUB_WINDOW_H */
