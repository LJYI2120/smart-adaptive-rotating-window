/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：sub_window.c
 * 模块说明：实现副窗非阻塞开关、限位、行程超时和堵转联动
 * 对应说明书章节：4.6 副窗执行控制
 */
#include "sub_window.h"
#include "config.h"
#include "hal.h"
#include "motor.h"

static sub_window_state_t g_state;
static uint32_t g_motion_start_ms;
static safety_status_t *g_safety;

/* 限位输入约定为高电平有效；真实板卡若使用常闭触点可在此统一取反。 */
static uint8_t sub_limit_closed_active(void)
{
  return (hal_gpio_read(LIMIT_PIN_SUB_CLOSED) != 0u) ? 1u : 0u;
}

static uint8_t sub_limit_open_active(void)
{
  return (hal_gpio_read(LIMIT_PIN_SUB_OPEN) != 0u) ? 1u : 0u;
}

/* 根据两个限位建立初始位置；两个同时有效属于异常，按故障处理。 */
static void sub_identify_position(void)
{
  uint8_t closed = sub_limit_closed_active();
  uint8_t open = sub_limit_open_active();

  if ((closed != 0u) && (open == 0u)) {
    g_state = SUB_WINDOW_CLOSED;
  } else if ((open != 0u) && (closed == 0u)) {
    g_state = SUB_WINDOW_OPEN;
  } else if ((open == 0u) && (closed == 0u)) {
    /*
     * 两端均未到位时窗口可能正停在中间，也可能限位线束失效。不得假定
     * 已关闭；打开请求会先转换为一次关闭归位，由行程超时保护兜底。
     */
    g_state = SUB_WINDOW_UNKNOWN;
  } else {
    g_state = SUB_WINDOW_FAULT;
    motor_mark_stall();
  }
}

/* 启动一次运动，记录周期起点并立即驱动电机。 */
static void sub_start_motion(sub_window_state_t moving_state,
                             motor_direction_t direction)
{
  if (motor_has_stall() != 0u) {
    g_state = SUB_WINDOW_FAULT;
    return;
  }

  g_state = moving_state;
  g_motion_start_ms = hal_get_ms();
  motor_set_sub_direction(direction);
}

/* 对应说明书：4.6.1 副窗初始化 */
void sub_window_init(void)
{
  g_safety = NULL;
  g_motion_start_ms = hal_get_ms();
  motor_set_sub_direction(MOTOR_DIR_STOP);
  sub_identify_position();
}

/*
 * 对应说明书：4.6.2 副窗打开
 * 已打开或正在打开时保持幂等，避免主循环重复调用造成启动时间重置。
 */
void sub_window_open(void)
{
  if ((g_state == SUB_WINDOW_OPEN) ||
      (g_state == SUB_WINDOW_OPENING)) {
    return;
  }

  /*
   * 未知位置不允许直接向外打开。先执行关闭归位，既缩短机构外伸时间，
   * 也通过关闭限位或行程超时确认位置。
   */
  if (g_state == SUB_WINDOW_UNKNOWN) {
    sub_window_close();
    return;
  }

  if (sub_limit_open_active() != 0u) {
    motor_set_sub_direction(MOTOR_DIR_STOP);
    g_state = SUB_WINDOW_OPEN;
    return;
  }

  sub_start_motion(SUB_WINDOW_OPENING, MOTOR_DIR_FORWARD);
}

/*
 * 对应说明书：4.6.3 副窗关闭
 * 关闭时若已经处于关闭限位，则直接清除驱动，不制造无意义的行程周期。
 */
void sub_window_close(void)
{
  if ((g_state == SUB_WINDOW_CLOSED) ||
      (g_state == SUB_WINDOW_CLOSING)) {
    return;
  }

  if (sub_limit_closed_active() != 0u) {
    motor_set_sub_direction(MOTOR_DIR_STOP);
    g_state = SUB_WINDOW_CLOSED;
    return;
  }

  sub_start_motion(SUB_WINDOW_CLOSING, MOTOR_DIR_REVERSE);
}

/* 记录一次执行器安全故障并停机。 */
static void sub_report_fault(void)
{
  motor_set_sub_direction(MOTOR_DIR_STOP);
  g_state = SUB_WINDOW_FAULT;
  if (g_safety != NULL) {
    g_safety->motor_stall = 1u;
  }
}

/*
 * 对应说明书：4.6.4 副窗周期服务
 * 顺序为电流堵转、目标限位、总行程超时。限位优先于超时，允许电机在
 * 最后采样点准时到位，不会因调度抖动产生误报。
 */
void sub_window_service(void)
{
  uint32_t elapsed;

  if ((g_state != SUB_WINDOW_OPENING) &&
      (g_state != SUB_WINDOW_CLOSING)) {
    return;
  }

  motor_stall_protect();
  if (motor_has_stall() != 0u) {
    sub_report_fault();
    return;
  }

  if ((g_state == SUB_WINDOW_OPENING) &&
      (sub_limit_open_active() != 0u)) {
    motor_set_sub_direction(MOTOR_DIR_STOP);
    g_state = SUB_WINDOW_OPEN;
    return;
  }

  if ((g_state == SUB_WINDOW_CLOSING) &&
      (sub_limit_closed_active() != 0u)) {
    motor_set_sub_direction(MOTOR_DIR_STOP);
    g_state = SUB_WINDOW_CLOSED;
    return;
  }

  elapsed = hal_get_ms() - g_motion_start_ms;
  if (elapsed >= SUB_WINDOW_TRAVEL_MS) {
    /*
     * 行程超时不区分机械卡死还是限位失效，统一按执行器堵转故障处理，
     * 这样安全层必然锁窗并要求人工检查。
     */
    motor_mark_stall();
    sub_report_fault();
  }
}

/* 对应说明书：4.6.7 副窗安全对象绑定 */
void sub_window_bind_safety_status(safety_status_t *safe)
{
  g_safety = safe;
}

sub_window_state_t sub_window_get_state(void)
{
  return g_state;
}

uint8_t sub_window_is_open(void)
{
  return (g_state == SUB_WINDOW_OPEN) ? 1u : 0u;
}

uint8_t sub_window_is_moving(void)
{
  if ((g_state == SUB_WINDOW_OPENING) ||
      (g_state == SUB_WINDOW_CLOSING)) {
    return 1u;
  }
  return 0u;
}

uint8_t sub_window_position_is_known(void)
{
  return ((g_state == SUB_WINDOW_CLOSED) ||
          (g_state == SUB_WINDOW_OPEN)) ? 1u : 0u;
}

/*
 * 对应说明书：4.6.8 执行器人工复位
 * 复位只清除软件锁存，最终位置仍由物理限位重新判定。
 */
void sub_window_reset(void)
{
  motor_set_sub_direction(MOTOR_DIR_STOP);
  motor_clear_stall();
  sub_identify_position();
  g_motion_start_ms = hal_get_ms();
}
