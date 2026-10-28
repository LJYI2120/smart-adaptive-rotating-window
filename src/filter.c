/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：filter.c
 * 模块说明：实现固定长度滑动平均与中值滤波
 * 对应说明书章节：4.2 采集数据滤波
 */
#include "filter.h"
#include "config.h"

#include <math.h>
#include <string.h>

/* 将外部长度约束到编译期最大容量，避免调用者误传长度造成越界。 */
static uint8_t filter_effective_len(uint8_t len)
{
  if (len == 0u) {
    return 0u;
  }
  if (len > (uint8_t)FILTER_MAX_WINDOW) {
    return (uint8_t)FILTER_MAX_WINDOW;
  }
  return len;
}

/* 把最旧值左移出窗口，并在末尾写入新值。 */
static void filter_push(float raw, float *buf, uint8_t len)
{
  uint8_t index;

  if ((buf == NULL) || (len < 2u)) {
    if ((buf != NULL) && (len == 1u)) {
      buf[0] = raw;
    }
    return;
  }

  for (index = 1u; index < len; ++index) {
    buf[index - 1u] = buf[index];
  }
  buf[len - 1u] = raw;
}

/*
 * 对应说明书：4.2.1 滑动平均滤波
 * 新样本即使为 NaN 也不写入历史窗口，避免一个异常点污染后续多个周期。
 * 若历史中已有非有限值，计算时跳过；当窗口无有效值时返回 0。
 */
float moving_average_filter(float raw, float *buf, uint8_t len)
{
  uint8_t count;
  uint8_t index;
  float sum = 0.0f;

  count = filter_effective_len(len);
  if ((buf == NULL) || (count == 0u)) {
    return 0.0f;
  }

  if (isfinite(raw)) {
    filter_push(raw, buf, count);
  }

  for (index = 0u; index < count; ++index) {
    if (isfinite(buf[index])) {
      sum += buf[index];
    }
  }

  /*
   * 第二次遍历取有效数量，避免在上面的累加循环中引入难以阅读的双重
   * 自增技巧；窗口固定较短，额外遍历不构成性能问题。
   */
  {
    uint8_t valid_count = 0u;
    for (index = 0u; index < count; ++index) {
      if (isfinite(buf[index])) {
        ++valid_count;
      }
    }
    if (valid_count == 0u) {
      return 0.0f;
    }
    return sum / (float)valid_count;
  }
}

/*
 * 对有限样本进行插入排序。复制到独立数组后再排序，保持调用者窗口的
 * 时间顺序不变，后续滑动平均仍能正确推进。
 */
static float filter_median_of_window(const float *buf, uint8_t len)
{
  float ordered[FILTER_MAX_WINDOW];
  uint8_t count = 0u;
  uint8_t index;
  uint8_t outer;

  if ((buf == NULL) || (len == 0u)) {
    return 0.0f;
  }

  for (index = 0u; index < len; ++index) {
    if (!isfinite(buf[index])) {
      continue;
    }
    ordered[count] = buf[index];
    ++count;
  }

  if (count == 0u) {
    return 0.0f;
  }

  for (outer = 1u; outer < count; ++outer) {
    float key = ordered[outer];
    uint8_t position = outer;

    while ((position > 0u) && (ordered[position - 1u] > key)) {
      ordered[position] = ordered[position - 1u];
      --position;
    }
    ordered[position] = key;
  }

  if ((count & 1u) != 0u) {
    return ordered[count / 2u];
  }
  return 0.5f * (ordered[(count / 2u) - 1u] + ordered[count / 2u]);
}

/*
 * 对应说明书：4.2.2 中值滤波
 * 中值滤波对雨滴、电磁脉冲造成的孤立尖峰不敏感；窗口长度由调用者
 * 指定并受 FILTER_MAX_WINDOW 限制。
 */
float median_filter(float raw, float *buf, uint8_t len)
{
  uint8_t count;

  count = filter_effective_len(len);
  if ((buf == NULL) || (count == 0u)) {
    return 0.0f;
  }

  if (isfinite(raw)) {
    filter_push(raw, buf, count);
  }

  return filter_median_of_window(buf, count);
}

/*
 * 对应说明书：4.2.3 滤波窗口初始化
 * 填满相同值使系统上电第一个样本就得到稳定输出，而不是由全零历史造成
 * 虚假的缓慢爬升。
 */
void filter_fill(float *buf, uint8_t len, float value)
{
  uint8_t count;
  uint8_t index;

  count = filter_effective_len(len);
  if ((buf == NULL) || (count == 0u) || !isfinite(value)) {
    return;
  }

  for (index = 0u; index < count; ++index) {
    buf[index] = value;
  }
}
