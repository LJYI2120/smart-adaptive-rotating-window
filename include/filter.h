/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：filter.h
 * 模块说明：声明固定窗口滑动平均与中值滤波
 * 对应说明书章节：4.2 采集数据滤波
 */
#ifndef SMART_WINDOW_FILTER_H
#define SMART_WINDOW_FILTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * buf 必须指向调用者拥有的 len 个 float 元素。函数把最旧样本移出，
 * 将 raw 写入末尾，并返回有限样本的平均值。这样无需隐藏静态索引，
 * 多路信号可并行使用独立缓冲区。
 */
float moving_average_filter(float raw, float *buf, uint8_t len);

/*
 * 同样的滑动窗口语义，使用有界插入排序求中值。len 超过编译期最大窗口
 * 时自动截断，避免栈数组越界。
 */
float median_filter(float raw, float *buf, uint8_t len);

/* 用指定值填满一个滤波窗口，常用于上电预热或测试。 */
void filter_fill(float *buf, uint8_t len, float value);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_FILTER_H */
