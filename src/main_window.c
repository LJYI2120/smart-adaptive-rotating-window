/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：main_window.c
 * 模块说明：实现主窗角度闭环、增量式PID和机械限位保护
 * 对应说明书章节：4.5 主窗闭环控制
 */
#include "main_window.h"
#include "config.h"
#include "hal.h"
#include "motor.h"

#include <math.h>

static float g_error_previous;
static float g_error_previous_2;
static float g_pid_output;
static float g_pid_increment;
static uint8_t g_control_direction;

/* 通用浮点限幅，NaN 回落到 0。 */
static float main_window_clamp(float value, float minimum, float maximum)
{
  if (!isfinite(value)) {
    return 0.0f;
  }
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

/* 清理 PID 历史。进入死区或出现故障后调用，防止积分记忆导致反向冲。 */
static void main_window_reset_pid(void)
{
  g_error_previous = 0.0f;
  g_error_previous_2 = 0.0f;
  g_pid_output = 0.0f;
  g_pid_increment = 0.0f;
}

/* 对应说明书：4.5.1 主窗控制器初始化 */
void main_window_init(void)
{
  main_window_reset_pid();
  g_control_direction = (uint8_t)MOTOR_DIR_STOP;
  hal_pwm_set(HAL_PWM_MAIN_CHANNEL, 0.0f);
}

/*
 * 对应说明书：4.5.2 双端机械限位
 * 到达边界本身是合法位置，因此使用闭区间。NaN 一律视为越限。
 * 任何越限都先停机，再向调用者报告失败。
 */
uint8_t limit_check(float angle)
{
  if (!isfinite(angle)) {
    hal_motor_stop();
    return 0u;
  }
  if ((angle < MAIN_ANGLE_MIN) || (angle > MAIN_ANGLE_MAX)) {
    hal_motor_stop();
    return 0u;
  }
  return 1u;
}

/*
 * 对应说明书：4.5.3 增量式PID
 * 第 k 周期误差 e[k] 的增量形式：
 *   dU = Kp*(e[k]-e[k-1]) + Ki*e[k] + Kd*(e[k]-2e[k-1]+e[k-2])
 * 输出累加后再限幅。KI 已按控制周期整定，核心层不依赖浮点系统时钟。
 */
static float main_window_compute_pid(float error)
{
  float proportional;
  float integral;
  float derivative;
  float delta;

  proportional = PID_KP * (error - g_error_previous);
  integral = PID_KI * error;
  derivative = PID_KD *
               (error - 2.0f * g_error_previous + g_error_previous_2);
  delta = proportional + integral + derivative;

  if (!isfinite(delta)) {
    main_window_reset_pid();
    return 0.0f;
  }

  g_pid_increment = fabsf(delta);
  g_pid_output += delta;
  g_pid_output = main_window_clamp(g_pid_output, 0.0f, 1.0f);

  g_error_previous_2 = g_error_previous;
  g_error_previous = error;
  return g_pid_output;
}

/*
 * 对应说明书：4.5.4 主窗角度闭环
 * 目标与反馈先做范围和 NaN 检查，再判断角度死区。进入死区立即停止
 * 电机并清除 PID 历史；误差方向决定正反转，PWM 幅值限制在 0~1。
 */
void main_window_angle_ctrl(float target_angle,
                            float current_angle,
                            float *pwm_out)
{
  float error;
  float absolute_error;
  float output;
  motor_direction_t direction;

  if ((limit_check(target_angle) == 0u) ||
      (limit_check(current_angle) == 0u)) {
    main_window_reset_pid();
    g_control_direction = (uint8_t)MOTOR_DIR_STOP;
    if (pwm_out != NULL) {
      *pwm_out = 0.0f;
    }
    motor_set_main_output(0.0f, MOTOR_DIR_STOP);
    return;
  }

  error = target_angle - current_angle;
  if (!isfinite(error)) {
    main_window_reset_pid();
    g_control_direction = (uint8_t)MOTOR_DIR_STOP;
    if (pwm_out != NULL) {
      *pwm_out = 0.0f;
    }
    motor_set_main_output(0.0f, MOTOR_DIR_STOP);
    return;
  }

  absolute_error = fabsf(error);
  if (absolute_error <= ANGLE_DEADBAND) {
    /*
     * 进入死区时主动停机，而不是保持一个极小 PWM。这样可减少静止发热、
     * 编码器噪声造成的方向反转，并满足防抖动要求。
     */
    main_window_reset_pid();
    g_control_direction = (uint8_t)MOTOR_DIR_STOP;
    if (pwm_out != NULL) {
      *pwm_out = 0.0f;
    }
    motor_set_main_output(0.0f, MOTOR_DIR_STOP);
    return;
  }

  output = main_window_compute_pid(error);
  direction = (error > 0.0f) ? MOTOR_DIR_FORWARD : MOTOR_DIR_REVERSE;
  g_control_direction = (uint8_t)direction;

  /*
   * 首次离开死区时 PID 输出可能仍为 0。为避免机构仅靠比例增量缓慢启动，
   * 使用执行层最小可驱动占空比是一个可调参数；这里保持算法输出语义，
   * 若结果为 0 仍视为停机，由下一周期积分累积后启动。
   */
  if (pwm_out != NULL) {
    *pwm_out = output;
  }
  motor_set_main_output(output, direction);
}

/* 对应说明书：4.5.5 主窗运动诊断 */
uint8_t main_window_get_direction(void)
{
  return g_control_direction;
}

/* 对应说明书：4.5.5 PID诊断 */
float main_window_get_pid_increment(void)
{
  return g_pid_increment;
}
