/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：hal_stub.c
 * 模块说明：使用静态存储实现可测试的主机硬件模拟层
 * 对应说明书章节：3.4 硬件抽象层
 */
#include "hal.h"
#include "config.h"

#include <math.h>
#include <string.h>

/*
 * 所有模拟资源均为文件级静态变量，进程生命周期内地址固定。该实现不使用
 * malloc/free，能够直接替换为 STM32 HAL 访问而不改变业务模块。
 */
static int g_adc_mv[HAL_MOCK_ADC_CHANNELS];
static float g_pwm_duty[HAL_MOCK_PWM_CHANNELS];
static uint8_t g_gpio[HAL_MOCK_GPIO_COUNT];
static int g_encoder_counts;
static uint64_t g_monotonic_us;
static uint32_t g_motor_stop_count;
static uint32_t g_delay_total_ms;
static uint32_t g_critical_depth;
static uint32_t g_critical_enter_count;

/* 将浮点占空比限制到驱动可接受范围，并拦截 NaN/Inf。 */
static float hal_clamp_duty(float duty)
{
  if (!isfinite(duty)) {
    return 0.0f;
  }
  if (duty < 0.0f) {
    return 0.0f;
  }
  if (duty > 1.0f) {
    return 1.0f;
  }
  return duty;
}

/* 对应说明书：3.4.1 模拟量采集 */
int hal_adc_read(uint8_t channel)
{
  if (channel >= HAL_MOCK_ADC_CHANNELS) {
    return ADC_DISCONNECT_MV;
  }
  return g_adc_mv[channel];
}

/* 对应说明书：3.4.2 PWM输出 */
void hal_pwm_set(uint8_t channel, float duty)
{
  if (channel >= HAL_MOCK_PWM_CHANNELS) {
    return;
  }
  g_pwm_duty[channel] = hal_clamp_duty(duty);
}

/* 对应说明书：3.4.3 编码器读取 */
int hal_encoder_read(void)
{
  return g_encoder_counts;
}

/* 对应说明书：3.4.4 数字输出 */
void hal_gpio_write(uint8_t pin, uint8_t value)
{
  if (pin >= HAL_MOCK_GPIO_COUNT) {
    return;
  }
  g_gpio[pin] = (value == 0u) ? 0u : 1u;
}

/* 对应说明书：3.4.4 数字输入 */
uint8_t hal_gpio_read(uint8_t pin)
{
  if (pin >= HAL_MOCK_GPIO_COUNT) {
    return 0u;
  }
  return g_gpio[pin];
}

/* 对应说明书：3.4.5 单调毫秒时钟 */
uint32_t hal_get_ms(void)
{
  return (uint32_t)(g_monotonic_us / 1000u);
}

/*
 * 停止所有执行器并记录调用次数。真实平台应同时拉低两侧方向和主副窗
 * PWM；模拟层把全部 PWM 清零，便于测试验证“限位即停电机”。
 */
void hal_motor_stop(void)
{
  uint8_t channel;

  for (channel = 0u; channel < HAL_MOCK_PWM_CHANNELS; ++channel) {
    g_pwm_duty[channel] = 0.0f;
  }
  g_gpio[MOTOR_PIN_MAIN_IN1] = 0u;
  g_gpio[MOTOR_PIN_MAIN_IN2] = 0u;
  g_gpio[MOTOR_PIN_SUB_IN1] = 0u;
  g_gpio[MOTOR_PIN_SUB_IN2] = 0u;
  ++g_motor_stop_count;
}

/*
 * 主机延时推进虚拟时钟。这样行程超时与手动命令超时可在测试中确定性地
 * 复现，不依赖真实墙钟。
 */
void hal_delay_ms(uint32_t milliseconds)
{
  g_monotonic_us += (uint64_t)milliseconds * 1000u;
  if (milliseconds <= (UINT32_MAX - g_delay_total_ms)) {
    g_delay_total_ms += milliseconds;
  } else {
    g_delay_total_ms = UINT32_MAX;
  }
}

/* 微秒延时只推进主机模拟时钟，用于验证换向死区路径。 */
void hal_delay_us(uint32_t microseconds)
{
  g_monotonic_us += (uint64_t)microseconds;
}

/*
 * 主机桩不支持真实中断，用深度计数模拟可嵌套临界区。移植到 MCU 时，
 * 必须在这两个函数中保存 PRIMASK、恢复原状态，而不是无条件开中断。
 */
void hal_critical_enter(void)
{
  if (g_critical_depth != UINT32_MAX) {
    ++g_critical_depth;
  }
  if (g_critical_enter_count != UINT32_MAX) {
    ++g_critical_enter_count;
  }
}

void hal_critical_exit(void)
{
  if (g_critical_depth > 0u) {
    --g_critical_depth;
  }
}

/* 对应说明书：10.1 主机测试环境初始化 */
void hal_reset_mock(void)
{
  memset(g_adc_mv, 0, sizeof(g_adc_mv));
  memset(g_pwm_duty, 0, sizeof(g_pwm_duty));
  memset(g_gpio, 0, sizeof(g_gpio));
  g_encoder_counts = 0;
  g_monotonic_us = 0u;
  g_motor_stop_count = 0u;
  g_delay_total_ms = 0u;
  g_critical_depth = 0u;
  g_critical_enter_count = 0u;
}

/* 测试辅助：设置ADC通道的毫伏值，非法毫伏被夹到ADC电源范围内。 */
void hal_set_mock_adc(uint8_t channel, int millivolts)
{
  if (channel >= HAL_MOCK_ADC_CHANNELS) {
    return;
  }
  if (millivolts < 0) {
    millivolts = 0;
  }
  if (millivolts > ADC_REF_MV) {
    millivolts = ADC_REF_MV;
  }
  g_adc_mv[channel] = millivolts;
}

/* 测试辅助：设置主窗编码器累计计数。 */
void hal_set_mock_encoder(int counts)
{
  g_encoder_counts = counts;
}

/* 测试辅助：设置限位或模拟数字输入。 */
void hal_set_mock_gpio(uint8_t pin, uint8_t value)
{
  hal_gpio_write(pin, value);
}

/* 测试辅助：直接推进毫秒计数，不模拟忙等时间。 */
void hal_set_mock_ms(uint32_t milliseconds)
{
  g_monotonic_us = (uint64_t)milliseconds * 1000u;
}

/* 以下只读接口用于断言模拟状态，不改变控制逻辑。 */
int hal_mock_get_adc(uint8_t channel)
{
  if (channel >= HAL_MOCK_ADC_CHANNELS) {
    return ADC_DISCONNECT_MV;
  }
  return g_adc_mv[channel];
}

float hal_mock_get_pwm(uint8_t channel)
{
  if (channel >= HAL_MOCK_PWM_CHANNELS) {
    return 0.0f;
  }
  return g_pwm_duty[channel];
}

int hal_mock_get_encoder(void)
{
  return g_encoder_counts;
}

uint8_t hal_mock_get_gpio(uint8_t pin)
{
  return hal_gpio_read(pin);
}

uint32_t hal_mock_get_ms(void)
{
  return hal_get_ms();
}

uint32_t hal_mock_get_stop_count(void)
{
  return g_motor_stop_count;
}

uint32_t hal_mock_get_delay_total_ms(void)
{
  return g_delay_total_ms;
}

uint32_t hal_mock_get_critical_enter_count(void)
{
  return g_critical_enter_count;
}
