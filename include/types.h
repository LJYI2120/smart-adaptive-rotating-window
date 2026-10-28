/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：types.h
 * 模块说明：定义系统公共数据模型、状态枚举与错误码
 * 对应说明书章节：3.2 系统数据模型
 */
#ifndef SMART_WINDOW_TYPES_H
#define SMART_WINDOW_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 状态值从高优先级到低优先级排列。上层日志和系统说明书中的迁移图均依赖
 * 这一固定顺序，因此不得为了插入新状态而改变既有状态的数值。
 */
typedef enum {
  ST_SAFE_LOCK = 0,
  ST_CLOSE_RAIN,
  ST_MANUAL,
  ST_VENTILATE,
  ST_AUTO_TRACK,
  ST_IDLE
} system_state_t;

/*
 * 环境数据快照。timestamp_ms 使用单调递增毫秒计数，允许 32 位自然回绕。
 * valid_mask 的各位定义见下方 SENSOR_VALID_* 宏。
 */
typedef struct {
  float temperature_c;
  float humidity_pct;
  float wind_speed_mps;
  float rain_level;
  float light_left;
  float light_right;
  float light_up;
  float light_down;
  uint32_t timestamp_ms;
  uint8_t valid_mask;
} env_data_t;

/*
 * 窗口执行命令。valid 表示本条命令已经经过决策器处理；
 * safety_lock 用于通知执行层当前不得接受普通控制动作。
 */
typedef struct {
  uint8_t valid;
  float target_main_angle_deg;
  uint8_t sub_window_open;
  uint8_t safety_lock;
  uint8_t manual_reset;
} window_cmd_t;

/*
 * 安全覆盖状态。每一项均为粘滞故障标志，正常条件恢复后仍由复位策略清除。
 */
typedef struct {
  uint8_t wind_lock;
  uint8_t rain_lock;
  uint8_t sensor_fault;
  uint8_t motor_stall;
  uint8_t comm_timeout;
} safety_status_t;

/*
 * 手动控制命令。valid 与 timestamp_ms 联合判断命令是否在有效窗口内，
 * 防止通信恢复后执行一条很旧的命令。
 */
typedef struct {
  uint8_t valid;
  float manual_angle_deg;
  uint8_t manual_sub_open;
  uint32_t timestamp_ms;
} manual_cmd_t;

/*
 * 统一错误码。公开控制函数保持稳定的 void 签名，本枚举主要供
 * 内部校验、主机测试和后续扩展诊断接口使用。
 */
typedef enum {
  APP_OK = 0,
  APP_ERR_NULL = 1,
  APP_ERR_RANGE = 2,
  APP_ERR_INVALID_STATE = 3,
  APP_ERR_TIMEOUT = 4,
  APP_ERR_STALL = 5,
  APP_ERR_COMM = 6,
  APP_ERR_IO = 7
} app_error_t;

/* valid_mask 的固定位定义，顺序与 env_data_t 的物理量顺序一致。 */
#define SENSOR_VALID_TEMPERATURE  ((uint8_t)0x01u)
#define SENSOR_VALID_HUMIDITY     ((uint8_t)0x02u)
#define SENSOR_VALID_WIND         ((uint8_t)0x04u)
#define SENSOR_VALID_RAIN         ((uint8_t)0x08u)
#define SENSOR_VALID_LIGHT_LEFT   ((uint8_t)0x10u)
#define SENSOR_VALID_LIGHT_RIGHT  ((uint8_t)0x20u)
#define SENSOR_VALID_LIGHT_UP     ((uint8_t)0x40u)
#define SENSOR_VALID_LIGHT_DOWN   ((uint8_t)0x80u)

#define SENSOR_VALID_ALL          ((uint8_t)0xFFu)
#define SENSOR_VALID_REQUIRED     SENSOR_VALID_ALL

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_TYPES_H */
