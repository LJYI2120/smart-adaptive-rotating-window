/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：sensor.h
 * 模块说明：声明环境采集、工程单位转换和有效性判定接口
 * 对应说明书章节：4.1 环境数据采集
 */
#ifndef SMART_WINDOW_SENSOR_H
#define SMART_WINDOW_SENSOR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 清空传感器缓存并建立确定性的初始状态。 */
void sensor_init(void);

/* 固定签名：采样全部环境通道并输出带有效性掩码的数据。 */
void env_sensor_read(env_data_t *out);

/* 固定签名：返回各物理量与原始通道共同决定的有效位掩码。 */
uint8_t sensor_valid_check(const env_data_t *in);

/*
 * 返回最近一次 env_sensor_read 使用的原始 ADC 数据。越界通道返回
 * ADC_DISCONNECT_MV，便于诊断面板和测试观察转换前状态。
 */
int sensor_get_raw_mv(uint8_t channel);

/* 返回最近一次有效性检查结果，不修改环境数据。 */
uint8_t sensor_get_valid_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_SENSOR_H */
