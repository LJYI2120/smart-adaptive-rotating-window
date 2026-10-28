/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：safety.c
 * 模块说明：实现风速雨量锁定、传感器故障、通信超时和复位
 * 对应说明书章节：4.7 安全覆盖与异常处理
 */
#include "safety.h"
#include "config.h"
#include "hal.h"
#include "motor.h"
#include "sensor.h"

#include <math.h>

static volatile uint8_t g_reset_requested;
static uint8_t g_motor_fault_seen;
static uint32_t g_motor_fault_since_ms;

/* 支持毫秒回绕的年龄计算，未来时间戳按超时处理。 */
static uint32_t safety_age_ms(uint32_t now, uint32_t then)
{
  uint32_t age = now - then;
  if (age > (UINT32_MAX / 2u)) {
    return UINT32_MAX;
  }
  return age;
}

/* 对应说明书：4.7.1 安全状态初始化 */
void safety_init(safety_status_t *safe)
{
  g_reset_requested = 0u;
  g_motor_fault_seen = 0u;
  g_motor_fault_since_ms = hal_get_ms();

  if (safe == NULL) {
    return;
  }
  safe->wind_lock = 0u;
  safe->rain_lock = 0u;
  safe->sensor_fault = 0u;
  safe->motor_stall = 0u;
  safe->comm_timeout = 0u;
}

/*
 * 对应说明书：4.7.2 风速锁定
 * 达到 WIND_LOCK_MPS 立即锁定；恢复时速度必须回落到锁定值减去
 * WIND_HYST 才清除，形成 1 m/s 的保护带。
 */
static void safety_update_wind(const env_data_t *env, safety_status_t *safe)
{
  if ((env == NULL) || !isfinite(env->wind_speed_mps)) {
    safe->wind_lock = 1u;
    return;
  }

  if (env->wind_speed_mps >= WIND_LOCK_MPS) {
    safe->wind_lock = 1u;
  } else if ((safe->wind_lock != 0u) &&
             (env->wind_speed_mps <= (WIND_LOCK_MPS - WIND_HYST))) {
    safe->wind_lock = 0u;
  }
}

/*
 * 对应说明书：4.7.3 雨量锁定
 * RAIN_DETECT 只触发 CLOSE_RAIN；真正安全锁要等雨量达到 RAIN_LOCK。
 */
static void safety_update_rain(const env_data_t *env, safety_status_t *safe)
{
  if ((env == NULL) || !isfinite(env->rain_level)) {
    safe->rain_lock = 1u;
    return;
  }

  if (env->rain_level >= RAIN_LOCK) {
    safe->rain_lock = 1u;
  } else if ((safe->rain_lock != 0u) &&
             (env->rain_level <= (RAIN_LOCK - RAIN_HYST))) {
    safe->rain_lock = 0u;
  }
}

/*
 * 对应说明书：4.7.4 传感器严重故障
 * 任一必需通道无效都视为严重故障，因为缺失风速或雨量无法保障关窗
 * 安全。有效位恢复后允许自动清除，安全状态机仍会维持锁存时间。
 */
static void safety_update_sensor(const env_data_t *env, safety_status_t *safe)
{
  uint8_t valid_mask;

  if (env == NULL) {
    safe->sensor_fault = 1u;
    return;
  }

  valid_mask = env->valid_mask;
  if ((valid_mask & SENSOR_VALID_REQUIRED) != SENSOR_VALID_REQUIRED) {
    /*
     * 兼容测试或低层调用只构造物理量而未设置 valid_mask：重新计算一次，
     * 避免把“未初始化字节”误判为硬件失效。
     */
    valid_mask = sensor_valid_check(env);
  }

  if ((valid_mask & SENSOR_VALID_REQUIRED) != SENSOR_VALID_REQUIRED) {
    safe->sensor_fault = 1u;
  } else {
    safe->sensor_fault = 0u;
  }
}

/*
 * 对应说明书：4.7.5 通信超时
 * 环境快照年龄超过 COMM_TIMEOUT_MS 时锁定。一旦时间戳刷新到超时窗口
 * 内，标志自动解除；无需把毫秒计数转换为墙钟。
 */
static void safety_update_comm(const env_data_t *env, safety_status_t *safe)
{
  uint32_t now;
  uint32_t age;

  if (env == NULL) {
    safe->comm_timeout = 1u;
    return;
  }

  now = hal_get_ms();
  age = safety_age_ms(now, env->timestamp_ms);
  if (age > COMM_TIMEOUT_MS) {
    safe->comm_timeout = 1u;
  } else {
    safe->comm_timeout = 0u;
  }
}

/*
 * 对应说明书：4.7.6 电机堵转粘滞与超时恢复
 * 堵转标志首次出现时记录时刻。默认永不自动清除；只有显式启用
 * MOTOR_STALL_AUTO_CLEAR_ENABLE 后，才允许在停机超时且电流恢复后清除。
 * 电机静止时电流正常并不能证明机械卡阻消失，因此自动恢复不是默认策略。
 */
static void safety_update_motor_timeout(safety_status_t *safe)
{
  uint32_t now = hal_get_ms();

  if (safe->motor_stall == 0u) {
    g_motor_fault_seen = 0u;
    g_motor_fault_since_ms = now;
    return;
  }

  if (g_motor_fault_seen == 0u) {
    g_motor_fault_seen = 1u;
    g_motor_fault_since_ms = now;
    return;
  }

#if MOTOR_STALL_AUTO_CLEAR_ENABLE
  if ((safety_age_ms(now, g_motor_fault_since_ms) >= SAFE_LOCK_RESET_MS) &&
      (motor_read_current_ma() <= STALL_CURRENT_MA)) {
    motor_clear_stall();
    safe->motor_stall = 0u;
    g_motor_fault_seen = 0u;
    g_motor_fault_since_ms = now;
  }
#else
  /*
   * 默认分支不执行自动清除。保留对 now 的读取用于状态诊断计时，
   * 也避免以后启用该策略时改变故障起始时刻的含义。
   */
  (void)now;
#endif
}

/*
 * 对应说明书：4.7 安全覆盖与异常处理
 * 本函数每个主循环周期执行，所有检查彼此独立，任何一项都能将系统推入
 * 安全锁，不依赖状态机当前处于哪一个普通模式。
 */
void safety_override(const env_data_t *env,
                     safety_status_t *safe)
{
  if (safe == NULL) {
    return;
  }

  safety_update_wind(env, safe);
  safety_update_rain(env, safe);
  safety_update_sensor(env, safe);
  safety_update_comm(env, safe);
  safety_update_motor_timeout(safe);
}

/* 通信中断与主循环之间使用单字请求，移植到 MCU 时可在临界区调用。 */
void safety_request_manual_reset(void)
{
  g_reset_requested = 1u;
}

/*
 * 读取后立即清零。即使两个复位帧连续到达，也在相邻主循环分别执行，
 * 不会被无锁计数器吞掉。
 */
uint8_t safety_consume_manual_reset(void)
{
  uint8_t requested = g_reset_requested;
  g_reset_requested = 0u;
  return requested;
}

/* 对应说明书：4.7.7 人工复位 */
void safety_clear_latches(safety_status_t *safe)
{
  if (safe == NULL) {
    return;
  }
  safe->wind_lock = 0u;
  safe->rain_lock = 0u;
  safe->sensor_fault = 0u;
  safe->comm_timeout = 0u;
  /*
   * motor_stall 由 motor_clear_stall 同步清除内部粘滞位；调用方应先确认
   * 物理危险已经排除。
   */
}

/* 对应说明书：4.7.8 安全状态汇总 */
uint8_t safety_any_lock(const safety_status_t *safe)
{
  if (safe == NULL) {
    return 1u;
  }
  if ((safe->wind_lock != 0u) ||
      (safe->rain_lock != 0u) ||
      (safe->sensor_fault != 0u) ||
      (safe->motor_stall != 0u) ||
      (safe->comm_timeout != 0u)) {
    return 1u;
  }
  return 0u;
}
