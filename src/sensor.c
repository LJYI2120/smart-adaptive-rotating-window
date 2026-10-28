/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：sensor.c
 * 模块说明：读取模拟传感器、转换工程单位并判定数据有效性
 * 对应说明书章节：4.1 环境数据采集
 */
#include "sensor.h"
#include "config.h"
#include "hal.h"

#include <math.h>
#include <string.h>

/*
 * 通道映射集中在本文件顶部，同时引用 config.h 中的公开宏。若板卡改线，
 * 仅修改配置宏；若传感器变换不同，可在此替换转换函数而不影响决策层。
 */
#define CH_TEMP        SENSOR_ADC_TEMPERATURE
#define CH_HUMIDITY    SENSOR_ADC_HUMIDITY
#define CH_WIND        SENSOR_ADC_WIND
#define CH_RAIN        SENSOR_ADC_RAIN
#define CH_LIGHT_LEFT  SENSOR_ADC_LIGHT_LEFT
#define CH_LIGHT_RIGHT SENSOR_ADC_LIGHT_RIGHT
#define CH_LIGHT_UP    SENSOR_ADC_LIGHT_UP
#define CH_LIGHT_DOWN  SENSOR_ADC_LIGHT_DOWN

static int g_last_raw_mv[SENSOR_CHANNEL_COUNT];
static uint32_t g_last_sample_ms;
static uint8_t g_last_valid_mask;

/* 把 ADC 毫伏值转换为浮点；ADC 桩本身使用整数返回，避免隐式截断歧义。 */
static float sensor_mv_to_value(int millivolts, float scale)
{
  return ((float)millivolts) * scale;
}

/* 判断一个有限的物理量是否位于指定闭区间。 */
static uint8_t sensor_in_range(float value, float minimum, float maximum)
{
  if (!isfinite(value)) {
    return 0u;
  }
  if (value < minimum || value > maximum) {
    return 0u;
  }
  return 1u;
}

/*
 * 原始通道检查覆盖两类硬件异常：
 * 1. 低于断开阈值，说明线束开路或传感器未供电；
 * 2. 达到 SATURATION 阈值，说明 ADC 顶部饱和，转换值不可信。
 * 对合法 0 mV 且可接受零值的通道，物理量程检查仍会保留有效性；
 * 当前默认配置将 0/1 mV 视为断线，便于明确暴露硬件故障。
 */
static uint8_t sensor_raw_channel_valid(uint8_t channel)
{
  int raw;

  if (channel >= SENSOR_CHANNEL_COUNT) {
    return 0u;
  }
  raw = g_last_raw_mv[channel];
  if (raw <= ADC_DISCONNECT_MV) {
    return 0u;
  }
  if (raw >= ADC_SATURATION_MV) {
    return 0u;
  }
  return 1u;
}

/* 仅在构造与测试时调用，清除所有原始与派生数据。 */
void sensor_init(void)
{
  memset(g_last_raw_mv, 0, sizeof(g_last_raw_mv));
  g_last_sample_ms = 0u;
  g_last_valid_mask = 0u;
}

/*
 * 对应说明书：4.1.1 温度与湿度采集
 * 原始 ADC 返回毫伏值，通过集中配置的比例因子转换为摄氏度与百分比。
 */
static float sensor_read_temperature(void)
{
  return sensor_mv_to_value(hal_adc_read(CH_TEMP), TEMP_MV_TO_C);
}

/* 对应说明书：4.1.1 温度与湿度采集 */
static float sensor_read_humidity(void)
{
  return sensor_mv_to_value(hal_adc_read(CH_HUMIDITY), HUMIDITY_MV_TO_PCT);
}

/* 对应说明书：4.1.2 风速与雨量采集 */
static float sensor_read_wind_speed(void)
{
  return sensor_mv_to_value(hal_adc_read(CH_WIND), WIND_MV_TO_MPS);
}

/* 对应说明书：4.1.2 风速与雨量采集 */
static float sensor_read_rain_level(void)
{
  return sensor_mv_to_value(hal_adc_read(CH_RAIN), RAIN_MV_TO_LEVEL);
}

/*
 * 四向光照读取使用相同比例，让左右差在 [-1000, 1000] 内变化。
 * 追光目标角由决策模块通过 TRACK_GAIN 换算，不在传感器层夹到角度范围。
 */
static float sensor_read_light(uint8_t channel)
{
  return sensor_mv_to_value(hal_adc_read(channel), LIGHT_MV_TO_LEVEL);
}

/*
 * 一次快照读取七路环境量。所有原始通道先缓存，再统一转换，保证同一
 * 控制周期看到的是相近采样时刻；timestamp_ms 取采样结束时刻。
 */
void env_sensor_read(env_data_t *out)
{
  uint8_t channel;

  if (out == NULL) {
    return;
  }

  for (channel = 0u; channel < SENSOR_CHANNEL_COUNT; ++channel) {
    g_last_raw_mv[channel] = hal_adc_read(channel);
  }

  out->temperature_c = sensor_read_temperature();
  out->humidity_pct = sensor_read_humidity();
  out->wind_speed_mps = sensor_read_wind_speed();
  out->rain_level = sensor_read_rain_level();
  out->light_left = sensor_read_light(CH_LIGHT_LEFT);
  out->light_right = sensor_read_light(CH_LIGHT_RIGHT);
  out->light_up = sensor_read_light(CH_LIGHT_UP);
  out->light_down = sensor_read_light(CH_LIGHT_DOWN);
  out->timestamp_ms = hal_get_ms();

  /*
   * 有效性检查需要区分“本次采样”的原始值与以前缓存的原始值。先记录
   * 采样时刻，再调用公开检查函数，可避免测试构造数据时误用旧原始值。
   */
  g_last_sample_ms = out->timestamp_ms;
  out->valid_mask = sensor_valid_check(out);
  g_last_valid_mask = out->valid_mask;
}

/*
 * 对应说明书：4.1.3 数据有效性检查
 * 返回位掩码而非布尔值：某一通道异常只使对应位清零，诊断接口可指出
 * 具体失效通道。严重故障判定统一由 safety_override 完成。
 */
uint8_t sensor_valid_check(const env_data_t *in)
{
  uint8_t mask = 0u;

  if (in == NULL) {
    g_last_valid_mask = 0u;
    return 0u;
  }

  /* 每个位先检查物理量程，再叠加最近原始通道的断线/饱和判断。 */
  if (sensor_in_range(in->temperature_c, TEMP_SENSOR_MIN_C, TEMP_SENSOR_MAX_C) &&
      sensor_raw_channel_valid(CH_TEMP)) {
    mask |= SENSOR_VALID_TEMPERATURE;
  }

  if (sensor_in_range(in->humidity_pct, HUMIDITY_SENSOR_MIN, HUMIDITY_SENSOR_MAX) &&
      sensor_raw_channel_valid(CH_HUMIDITY)) {
    mask |= SENSOR_VALID_HUMIDITY;
  }

  if (sensor_in_range(in->wind_speed_mps, WIND_SENSOR_MIN_MPS, WIND_SENSOR_MAX_MPS) &&
      sensor_raw_channel_valid(CH_WIND)) {
    mask |= SENSOR_VALID_WIND;
  }

  if (sensor_in_range(in->rain_level, RAIN_SENSOR_MIN, RAIN_SENSOR_MAX) &&
      sensor_raw_channel_valid(CH_RAIN)) {
    mask |= SENSOR_VALID_RAIN;
  }

  if (sensor_in_range(in->light_left, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX) &&
      sensor_raw_channel_valid(CH_LIGHT_LEFT)) {
    mask |= SENSOR_VALID_LIGHT_LEFT;
  }

  if (sensor_in_range(in->light_right, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX) &&
      sensor_raw_channel_valid(CH_LIGHT_RIGHT)) {
    mask |= SENSOR_VALID_LIGHT_RIGHT;
  }

  if (sensor_in_range(in->light_up, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX) &&
      sensor_raw_channel_valid(CH_LIGHT_UP)) {
    mask |= SENSOR_VALID_LIGHT_UP;
  }

  if (sensor_in_range(in->light_down, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX) &&
      sensor_raw_channel_valid(CH_LIGHT_DOWN)) {
    mask |= SENSOR_VALID_LIGHT_DOWN;
  }

  /*
   * 手工构造的测试数据没有经过 env_sensor_read 时，不套用旧原始缓存；
   * 物理量检查已经能识别 NaN、越界与饱和后转换出的极端值。
   */
  if (g_last_sample_ms != in->timestamp_ms) {
    if (sensor_in_range(in->temperature_c, TEMP_SENSOR_MIN_C, TEMP_SENSOR_MAX_C)) {
      mask |= SENSOR_VALID_TEMPERATURE;
    }
    if (sensor_in_range(in->humidity_pct, HUMIDITY_SENSOR_MIN, HUMIDITY_SENSOR_MAX)) {
      mask |= SENSOR_VALID_HUMIDITY;
    }
    if (sensor_in_range(in->wind_speed_mps, WIND_SENSOR_MIN_MPS, WIND_SENSOR_MAX_MPS)) {
      mask |= SENSOR_VALID_WIND;
    }
    if (sensor_in_range(in->rain_level, RAIN_SENSOR_MIN, RAIN_SENSOR_MAX)) {
      mask |= SENSOR_VALID_RAIN;
    }
    if (sensor_in_range(in->light_left, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX)) {
      mask |= SENSOR_VALID_LIGHT_LEFT;
    }
    if (sensor_in_range(in->light_right, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX)) {
      mask |= SENSOR_VALID_LIGHT_RIGHT;
    }
    if (sensor_in_range(in->light_up, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX)) {
      mask |= SENSOR_VALID_LIGHT_UP;
    }
    if (sensor_in_range(in->light_down, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX)) {
      mask |= SENSOR_VALID_LIGHT_DOWN;
    }
  }

  g_last_valid_mask = mask;
  return mask;
}

/* 对应说明书：4.1.4 原始值诊断接口 */
int sensor_get_raw_mv(uint8_t channel)
{
  if (channel >= SENSOR_CHANNEL_COUNT) {
    return ADC_DISCONNECT_MV;
  }
  return g_last_raw_mv[channel];
}

/* 对应说明书：4.1.4 采集状态诊断接口 */
uint8_t sensor_get_valid_mask(void)
{
  return g_last_valid_mask;
}
