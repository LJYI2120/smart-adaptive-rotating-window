/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：decision.c
 * 模块说明：实现环境优先级仲裁、舒适区迟滞和主副窗动作生成
 * 对应说明书章节：4.3 决策仲裁逻辑
 */
#include "decision.h"
#include "config.h"
#include "hal.h"

#include <math.h>

/* 统一处理浮点上下限，NaN 被替换为指定安全值。 */
static float decision_clamp_with_default(float value,
                                         float minimum,
                                         float maximum,
                                         float fallback)
{
  if (!isfinite(value)) {
    return fallback;
  }
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

/* 对应说明书：4.3.4 主窗角度约束 */
float decision_clamp_angle(float angle)
{
  return decision_clamp_with_default(angle,
                                     MAIN_ANGLE_MIN,
                                     MAIN_ANGLE_MAX,
                                     SAFE_ANGLE);
}

/*
 * 32 位毫秒差直接相减即可支持自然回绕。若差值落在无符号范围的后半区，
 * 说明时间戳在未来，按超时处理，避免异常数据永久占据手动优先级。
 */
static uint32_t decision_elapsed_ms(uint32_t now, uint32_t then)
{
  uint32_t elapsed = now - then;
  if (elapsed > (UINT32_MAX / 2u)) {
    return UINT32_MAX;
  }
  return elapsed;
}

/*
 * 对应说明书：4.3.3 手动命令时效
 * now 优先使用本次环境快照的时间，保证仲裁和传感器数据属于同一控制周期。
 */
uint8_t decision_manual_is_valid(const env_data_t *env,
                                 const manual_cmd_t *manual)
{
  uint32_t now;
  uint32_t age;

  if ((manual == NULL) || (manual->valid == 0u)) {
    return 0u;
  }
  if (!isfinite(manual->manual_angle_deg)) {
    return 0u;
  }
  if ((manual->manual_angle_deg < MAIN_ANGLE_MIN) ||
      (manual->manual_angle_deg > MAIN_ANGLE_MAX)) {
    return 0u;
  }

  now = (env == NULL) ? hal_get_ms() : env->timestamp_ms;
  age = decision_elapsed_ms(now, manual->timestamp_ms);
  if (age > MANUAL_TIMEOUT_MS) {
    return 0u;
  }
  return 1u;
}

/* 安全标志中任意一项非零都要求进入最高优先级锁定状态。 */
static uint8_t decision_any_safety_lock(const env_data_t *env,
                                        const safety_status_t *safe)
{
  if (safe != NULL) {
    if ((safe->wind_lock != 0u) ||
        (safe->rain_lock != 0u) ||
        (safe->sensor_fault != 0u) ||
        (safe->motor_stall != 0u) ||
        (safe->comm_timeout != 0u)) {
      return 1u;
    }
  }

  /*
   * 即使安全模块尚未运行，决策器仍直接检查可确定的极端环境量。
   * 这一冗余门限防止单模块调度遗漏使窗口在高风速下继续追光。
   */
  if ((env != NULL) && isfinite(env->wind_speed_mps) &&
      (env->wind_speed_mps >= WIND_LOCK_MPS)) {
    return 1u;
  }
  if ((env != NULL) && isfinite(env->rain_level) &&
      (env->rain_level >= RAIN_LOCK)) {
    return 1u;
  }
  return 0u;
}

/*
 * 舒适区判断采用入口与保持两组边界：
 * - 尚未通风时，温度严格越过舒适上下限才触发；
 * - 已通风时，必须回到限值内侧一个迟滞量才释放。
 */
static uint8_t decision_needs_ventilation(const env_data_t *env,
                                          system_state_t prior)
{
  float temperature_min = TEMP_COMFORT_MIN;
  float temperature_max = TEMP_COMFORT_MAX;
  float humidity_min = HUMIDITY_COMFORT_MIN;
  float humidity_max = HUMIDITY_COMFORT_MAX;
  uint8_t temperature_bad;
  uint8_t humidity_bad;

  if (env == NULL) {
    return 0u;
  }
  if (!isfinite(env->temperature_c) || !isfinite(env->humidity_pct)) {
    return 0u;
  }

  if (prior == ST_VENTILATE) {
    temperature_min += TEMP_HYST;
    temperature_max -= TEMP_HYST;
    humidity_min += HUM_HYST;
    humidity_max -= HUM_HYST;
  }

  temperature_bad = ((env->temperature_c < temperature_min) ||
                     (env->temperature_c > temperature_max)) ? 1u : 0u;
  humidity_bad = ((env->humidity_pct < humidity_min) ||
                  (env->humidity_pct > humidity_max)) ? 1u : 0u;
  return (uint8_t)((temperature_bad != 0u) || (humidity_bad != 0u));
}

/* 风速处于保护带时不允许通风，防止阵风反复推动副窗机构。 */
static uint8_t decision_wind_allows_ventilation(const env_data_t *env)
{
  if ((env == NULL) || !isfinite(env->wind_speed_mps)) {
    return 0u;
  }
  return (env->wind_speed_mps < WIND_SAFE_MPS) ? 1u : 0u;
}

/* 雨滴检测入口与退出共用迟滞，关闭状态不会在阈值附近抖动。 */
static uint8_t decision_rain_requires_close(const env_data_t *env,
                                            system_state_t prior)
{
  float threshold = RAIN_DETECT;

  if ((env == NULL) || !isfinite(env->rain_level)) {
    return 0u;
  }
  if (prior == ST_CLOSE_RAIN) {
    threshold -= RAIN_HYST;
  }
  return (env->rain_level >= threshold) ? 1u : 0u;
}

/*
 * 对应说明书：4.3.5 自动追光角度
 * 水平光差决定旋转方向和幅值；垂直光差参与“是否需要追光”的判断，
 * 但目标角严格采用主提示词指定的水平差公式。
 */
static float decision_track_angle(const env_data_t *env)
{
  float horizontal_difference;
  float target;

  if ((env == NULL) || !isfinite(env->light_left) ||
      !isfinite(env->light_right)) {
    return SAFE_ANGLE;
  }

  horizontal_difference = env->light_left - env->light_right;
  target = horizontal_difference * TRACK_GAIN;
  return decision_clamp_angle(target);
}

/* 任一向光照差超过各自门限时，认定为存在追光需求。 */
static uint8_t decision_light_requires_track(const env_data_t *env)
{
  float horizontal_difference;
  float vertical_difference;
  float vertical_threshold;

  if ((env == NULL) || !isfinite(env->light_left) ||
      !isfinite(env->light_right) || !isfinite(env->light_up) ||
      !isfinite(env->light_down)) {
    return 0u;
  }

  horizontal_difference = fabsf(env->light_left - env->light_right);
  vertical_difference = fabsf(env->light_up - env->light_down);
  vertical_threshold = LIGHT_DEADBAND * LIGHT_VERTICAL_FACTOR;

  if ((horizontal_difference > LIGHT_DEADBAND) ||
      (vertical_difference > vertical_threshold)) {
    return 1u;
  }
  return 0u;
}

/* IDLE 延续上一次有限目标角，避免进入保持状态时突然回零。 */
static float decision_hold_angle(const window_cmd_t *cmd)
{
  if ((cmd != NULL) && isfinite(cmd->target_main_angle_deg)) {
    return decision_clamp_angle(cmd->target_main_angle_deg);
  }
  return SAFE_ANGLE;
}

/* 将命令字段统一写入一个安全、完整的动作，防止部分更新留下旧副窗值。 */
static void decision_commit(window_cmd_t *cmd,
                            float angle,
                            uint8_t sub_open,
                            uint8_t safety_lock)
{
  if (cmd == NULL) {
    return;
  }
  cmd->valid = 1u;
  cmd->target_main_angle_deg = decision_clamp_angle(angle);
  cmd->sub_window_open = (sub_open == 0u) ? 0u : 1u;
  cmd->safety_lock = (safety_lock == 0u) ? 0u : 1u;
}

/*
 * 对应说明书：4.3 决策仲裁逻辑
 * 主提示词规定的优先级在函数内按自上而下顺序实现，后置规则不会覆盖
 * 更高优先级动作。调用者传入的 manual_reset 在入口保留。
 */
void decision_arbitrate(const env_data_t *env,
                        const manual_cmd_t *manual,
                        const safety_status_t *safe,
                        window_cmd_t *cmd,
                        system_state_t *state)
{
  system_state_t prior;
  uint8_t manual_reset;

  if (cmd == NULL) {
    return;
  }

  prior = (state == NULL) ? ST_IDLE : *state;
  manual_reset = cmd->manual_reset;

  /* 优先级 1：安全覆盖。窗口关闭副窗并回到安全角，等待锁存复位。 */
  if (decision_any_safety_lock(env, safe) != 0u) {
    decision_commit(cmd, SAFE_ANGLE, 0u, 1u);
    cmd->manual_reset = manual_reset;
    if (state != NULL) {
      *state = ST_SAFE_LOCK;
    }
    return;
  }

  /* 优先级 2：雨滴保护。未到锁定级时只关闭窗口，不产生整机安全锁。 */
  if (decision_rain_requires_close(env, prior) != 0u) {
    decision_commit(cmd, RAIN_ANGLE, 0u, 0u);
    cmd->manual_reset = manual_reset;
    if (state != NULL) {
      *state = ST_CLOSE_RAIN;
    }
    return;
  }

  /*
   * 优先级 3：手动。有效命令覆盖舒适、追光和保持；超出 30 秒后自然
   * 落到后续自动规则，不需要额外的“自动恢复”伪状态。
   */
  if (decision_manual_is_valid(env, manual) != 0u) {
    decision_commit(cmd,
                    manual->manual_angle_deg,
                    manual->manual_sub_open,
                    0u);
    cmd->manual_reset = manual_reset;
    if (state != NULL) {
      *state = ST_MANUAL;
    }
    return;
  }

  /*
   * 优先级 4：通风。温度或湿度超出舒适区，并且当前风速低于安全入口；
   * 雨量已在上一分支处理。副窗打开，主窗使用固定通风角。
   */
  if ((decision_needs_ventilation(env, prior) != 0u) &&
      (decision_wind_allows_ventilation(env) != 0u)) {
    decision_commit(cmd, VENT_ANGLE, 1u, 0u);
    cmd->manual_reset = manual_reset;
    if (state != NULL) {
      *state = ST_VENTILATE;
    }
    return;
  }

  /*
   * 优先级 5：追光。水平或垂直光差超过死区后旋转主窗，副窗关闭；
   * 光照无效时不会进入追光，最终落到 IDLE。
   */
  if (decision_light_requires_track(env) != 0u) {
    decision_commit(cmd, decision_track_angle(env), 0u, 0u);
    cmd->manual_reset = manual_reset;
    if (state != NULL) {
      *state = ST_AUTO_TRACK;
    }
    return;
  }

  /*
   * 优先级 6：节能保持。光源进入死区后保留旧角度，副窗关闭。
   * 不在此处停 PWM，执行层角度死区会进一步抑制微小抖动。
   */
  decision_commit(cmd, decision_hold_angle(cmd), 0u, 0u);
  cmd->manual_reset = manual_reset;
  if (state != NULL) {
    *state = ST_IDLE;
  }
}
