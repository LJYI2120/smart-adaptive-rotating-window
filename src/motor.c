/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：motor.c
 * 模块说明：封装主副窗方向、PWM、电流采样和堵转故障
 * 对应说明书章节：4.6 电机执行层
 */
#include "motor.h"
#include "config.h"
#include "hal.h"

#include <math.h>

static safety_status_t *g_bound_safety;
static uint8_t g_stall_latched;
static uint8_t g_main_active;
static uint8_t g_sub_active;
static motor_direction_t g_sub_direction;

/* 把方向枚举转换为左右方向脚的互补输出。 */
static void motor_write_main_direction(motor_direction_t direction)
{
  uint8_t forward = 0u;
  uint8_t reverse = 0u;

  if (direction == MOTOR_DIR_FORWARD) {
    forward = 1u;
  } else if (direction == MOTOR_DIR_REVERSE) {
    reverse = 1u;
  }

  hal_gpio_write(MOTOR_PIN_MAIN_IN1, forward);
  hal_gpio_write(MOTOR_PIN_MAIN_IN2, reverse);
}

/* 副窗方向脚分布与主窗相同，独立通道避免两个机构互相影响。 */
static void motor_write_sub_direction(motor_direction_t direction)
{
  uint8_t forward = 0u;
  uint8_t reverse = 0u;

  if (direction == MOTOR_DIR_FORWARD) {
    forward = 1u;
  } else if (direction == MOTOR_DIR_REVERSE) {
    reverse = 1u;
  }

  hal_gpio_write(MOTOR_PIN_SUB_IN1, forward);
  hal_gpio_write(MOTOR_PIN_SUB_IN2, reverse);
}

/* 对占空比做范围与 NaN 防护；无效输入等价于停机。 */
static float motor_sanitize_duty(float duty)
{
  if (!isfinite(duty) || (duty <= 0.0f)) {
    return PWM_MIN_DUTY;
  }
  if (duty > PWM_MAX_DUTY) {
    return PWM_MAX_DUTY;
  }
  return duty;
}

/*
 * 对应说明书：4.6.1 电机初始化
 * 初始化先关断所有方向与 PWM，再清内部故障，防止上电瞬间机构动作。
 */
void motor_init(void)
{
  g_bound_safety = NULL;
  g_stall_latched = 0u;
  g_main_active = 0u;
  g_sub_active = 0u;
  g_sub_direction = MOTOR_DIR_STOP;

  motor_write_main_direction(MOTOR_DIR_STOP);
  motor_write_sub_direction(MOTOR_DIR_STOP);
  hal_pwm_set(HAL_PWM_MAIN_CHANNEL, 0.0f);
  hal_pwm_set(HAL_PWM_SUB_CHANNEL, 0.0f);
}

/*
 * 对应说明书：4.6.7 堵转与安全状态联动
 * 安全对象由 app_init 绑定；不采用全局可执行回调，便于静态分析。
 */
void motor_bind_safety_status(safety_status_t *safe)
{
  g_bound_safety = safe;
}

/* 对应说明书：4.5.3 编码器反馈 */
float motor_read_main_angle_deg(void)
{
  int counts;

  if (ENCODER_COUNTS_PER_DEG <= 0.0f) {
    return 0.0f;
  }

  counts = hal_encoder_read() - ENCODER_ZERO_COUNTS;
  return ((float)counts) / ENCODER_COUNTS_PER_DEG;
}

/*
 * 对应说明书：4.6.5 电流反馈
 * 模拟采样值以 mV 返回。CURRENT_MV_PER_AMP 表示每安培对应的 mV，
 * 因此 mV/比例得到 A，再乘 1000 转为 mA。
 */
int motor_read_current_ma(void)
{
  int millivolts;
  float current_ma;

  millivolts = hal_adc_read(ADC_CHANNEL_MOTOR_CURRENT);
  if ((millivolts < 0) || (millivolts > ADC_REF_MV)) {
    return 0;
  }

  current_ma = ((float)millivolts) / CURRENT_MV_PER_AMP *
               CURRENT_MA_PER_AMP -
               (float)CURRENT_SAMPLE_MA_OFFSET;
  if (!isfinite(current_ma) || (current_ma < 0.0f)) {
    return 0;
  }
  if (current_ma > (float)MOTOR_CURRENT_MAX_MA) {
    return MOTOR_CURRENT_MAX_MA;
  }
  return (int)(current_ma + 0.5f);
}

/*
 * 对应说明书：4.5.4 主窗PWM与方向驱动
 * 先判断历史堵转，故障锁存期间拒绝任何新运动请求。正常值被写入主窗
 * PWM，方向和占空比同时更新。
 */
void motor_set_main_output(float duty, motor_direction_t direction)
{
  float safe_duty = motor_sanitize_duty(duty);

  if (g_stall_latched != 0u) {
    motor_stop_all();
    return;
  }

  if ((safe_duty == 0.0f) || (direction == MOTOR_DIR_STOP)) {
    motor_write_main_direction(MOTOR_DIR_STOP);
    hal_pwm_set(HAL_PWM_MAIN_CHANNEL, 0.0f);
    g_main_active = 0u;
    return;
  }

  /* 先撤PWM，再切换方向，给功率桥留出软件侧死区。 */
  hal_pwm_set(HAL_PWM_MAIN_CHANNEL, PWM_MIN_DUTY);
  motor_write_main_direction(direction);
  hal_pwm_set(HAL_PWM_MAIN_CHANNEL, safe_duty);
  g_main_active = 1u;
}

/*
 * 对应说明书：4.6.2 副窗驱动
 * 副窗 PWM 使用配置周期间接定义的固定速度；PWM 值集中在本函数，业务层
 * 只决定开关方向。
 */
void motor_set_sub_direction(motor_direction_t direction)
{
  if (g_stall_latched != 0u) {
    motor_write_sub_direction(MOTOR_DIR_STOP);
    hal_pwm_set(HAL_PWM_SUB_CHANNEL, 0.0f);
    g_sub_direction = MOTOR_DIR_STOP;
    return;
  }

  if (direction == MOTOR_DIR_STOP) {
    motor_write_sub_direction(MOTOR_DIR_STOP);
    hal_pwm_set(HAL_PWM_SUB_CHANNEL, 0.0f);
    g_sub_direction = MOTOR_DIR_STOP;
    g_sub_active = 0u;
    return;
  }

  /* 换向前先关断副窗PWM，避免半桥上下管瞬时直通。 */
  hal_pwm_set(HAL_PWM_SUB_CHANNEL, PWM_MIN_DUTY);
  motor_write_sub_direction(direction);
  /*
   * 0.65 是执行器机械特性参数而非环境阈值；为避免业务配置表混入
   * 驱动细节，在电机层用带注释的静态常量表达。
   */
  {
    hal_pwm_set(HAL_PWM_SUB_CHANNEL, SUB_MOTOR_DUTY);
  }
  g_sub_direction = direction;
  g_sub_active = 1u;
}

/* 对应说明书：4.6.6 统一停机 */
void motor_stop_all(void)
{
  motor_write_main_direction(MOTOR_DIR_STOP);
  motor_write_sub_direction(MOTOR_DIR_STOP);
  hal_pwm_set(HAL_PWM_MAIN_CHANNEL, 0.0f);
  hal_pwm_set(HAL_PWM_SUB_CHANNEL, 0.0f);
  g_sub_direction = MOTOR_DIR_STOP;
  g_main_active = 0u;
  g_sub_active = 0u;
  hal_motor_stop();
}

/*
 * 对应说明书：4.6.7 电流堵转保护
 * 只在本层存在有效驱动时判断电流，避免静止保持电流被误报为堵转。
 * 阈值比较使用严格大于，等于门限仍允许运行，方便边界测试复现。
 */
void motor_stall_protect(void)
{
  int current_ma;

  if ((g_main_active == 0u) && (g_sub_active == 0u)) {
    return;
  }

  current_ma = motor_read_current_ma();
  if (current_ma > STALL_CURRENT_MA) {
    g_stall_latched = 1u;
    motor_stop_all();
    if (g_bound_safety != NULL) {
      g_bound_safety->motor_stall = 1u;
    }
  }
}

/* 行程层检测到机构无位移但仍在驱动时，主动登记同一类堵转故障。 */
void motor_mark_stall(void)
{
  g_stall_latched = 1u;
  motor_stop_all();
  if (g_bound_safety != NULL) {
    g_bound_safety->motor_stall = 1u;
  }
}

/* 对应说明书：4.6.8 堵转诊断 */
uint8_t motor_has_stall(void)
{
  return g_stall_latched;
}

/* 查询当前是否存在主窗或副窗驱动。 */
uint8_t motor_is_drive_active(void)
{
  return ((g_main_active != 0u) || (g_sub_active != 0u)) ? 1u : 0u;
}

/* 返回副窗最近驱动方向；停止状态为 MOTOR_DIR_STOP。 */
motor_direction_t motor_get_sub_direction(void)
{
  return g_sub_direction;
}

/*
 * 对应说明书：4.5 安全恢复
 * 只清除软件粘滞位；危险环境仍存在时，上层安全覆盖会在下一周期重新
 * 置位。物理故障未排除前不应调用本函数。
 */
void motor_clear_stall(void)
{
  g_stall_latched = 0u;
  if (g_bound_safety != NULL) {
    g_bound_safety->motor_stall = 0u;
  }
}
