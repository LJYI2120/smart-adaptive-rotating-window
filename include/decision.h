/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：decision.h
 * 模块说明：声明优先级仲裁、追光计算和状态上传入口
 * 对应说明书章节：4.3 决策仲裁逻辑
 */
#ifndef SMART_WINDOW_DECISION_H
#define SMART_WINDOW_DECISION_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 固定签名：按照安全、雨天、手动、通风、追光、保持的顺序计算窗口命令。
 * state 同时作为输入和输出；输入值用于舒适区迟滞与 IDLE 保持。
 */
void decision_arbitrate(const env_data_t *env,
                        const manual_cmd_t *manual,
                        const safety_status_t *safe,
                        window_cmd_t *cmd,
                        system_state_t *state);

/* 将角度限制在主窗机械范围内；NaN 返回安全角。 */
float decision_clamp_angle(float angle);

/* 判断手动命令是否有效、数值合法且未超过 MANUAL_TIMEOUT_MS。 */
uint8_t decision_manual_is_valid(const env_data_t *env,
                                 const manual_cmd_t *manual);

/*
 * 解析八字节手动帧：
 * [0]=A5, [1]=CRC8, [2..5]=float小端角度, [6]=标志, [7]=保留。
 * 标志 bit0 表示打开副窗，bit1 表示请求安全复位。
 */
void manual_cmd_parse(const uint8_t *buf, uint16_t len, manual_cmd_t *out);

/* 状态上传快照，使用二进制字段而不是字符串，避免核心层依赖 printf。 */
typedef struct {
  uint32_t timestamp_ms;
  float target_main_angle_deg;
  float wind_speed_mps;
  float rain_level;
  system_state_t state;
  uint8_t valid_mask;
  uint8_t sub_window_open;
  uint8_t safety_lock;
} status_snapshot_t;

/*
 * 固定签名：上传一条固定长度状态快照。实现位于 app.c，使用静态缓冲，
 * 不依赖 printf 或动态内存。
 */
void status_upload(const env_data_t *env,
                   const window_cmd_t *cmd,
                   system_state_t state);

uint32_t status_get_upload_count(void);
const status_snapshot_t *status_get_latest(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_DECISION_H */
