/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：state_machine.c
 * 模块说明：实现优先级状态迁移、稳定确认和安全锁存恢复
 * 对应说明书章节：4.4 工作状态机
 */
#include "state_machine.h"
#include "decision.h"
#include "config.h"
#include "hal.h"

static uint8_t g_safe_latched;
static uint32_t g_safe_latched_since_ms;
static system_state_t g_pending_state;
static uint32_t g_pending_since_ms;
static uint8_t g_pending_valid;

/* 安全状态立即迁移，其余自动状态共用稳定确认窗口。 */
static uint8_t state_is_immediate(system_state_t state)
{
  if ((state == ST_SAFE_LOCK) ||
      (state == ST_CLOSE_RAIN) ||
      (state == ST_MANUAL)) {
    return 1u;
  }
  return 0u;
}

/* 支持毫秒计数器回绕的经过时间计算。 */
static uint32_t state_elapsed_ms(uint32_t now, uint32_t then)
{
  return now - then;
}

/*
 * 安全锁存只在两种条件之一满足时释放：
 * 1. 本周期收到手动复位脉冲；
 * 2. 锁存时间达到 SAFE_LOCK_RESET_MS。
 * 当前安全标志仍非零时，即使超时也不会离开安全状态。
 */
static uint8_t state_can_release_safe(const safety_status_t *safe,
                                      uint32_t now,
                                      uint8_t manual_reset)
{
  if ((safe != NULL) &&
      ((safe->wind_lock != 0u) ||
       (safe->rain_lock != 0u) ||
       (safe->sensor_fault != 0u) ||
       (safe->motor_stall != 0u) ||
       (safe->comm_timeout != 0u))) {
    return 0u;
  }

  if (manual_reset != 0u) {
    return 1u;
  }
  if (state_elapsed_ms(now, g_safe_latched_since_ms) >= SAFE_LOCK_RESET_MS) {
    return 1u;
  }
  return 0u;
}

/* 自动模式候选必须连续保持 STATE_CONFIRM_MS 后才正式提交。 */
static uint8_t state_confirm_candidate(system_state_t candidate,
                                       uint32_t now)
{
  if (g_pending_valid == 0u) {
    g_pending_state = candidate;
    g_pending_since_ms = now;
    g_pending_valid = 1u;
    return 0u;
  }

  if (g_pending_state != candidate) {
    g_pending_state = candidate;
    g_pending_since_ms = now;
    return 0u;
  }

  if (state_elapsed_ms(now, g_pending_since_ms) < STATE_CONFIRM_MS) {
    return 0u;
  }
  return 1u;
}

/* 对应说明书：4.4.1 状态机初始化 */
void state_machine_reset(void)
{
  g_safe_latched = 0u;
  g_safe_latched_since_ms = hal_get_ms();
  g_pending_state = ST_IDLE;
  g_pending_since_ms = hal_get_ms();
  g_pending_valid = 0u;
}

/* 对应说明书：4.5 安全锁恢复条件 */
uint8_t state_machine_safe_is_latched(void)
{
  return g_safe_latched;
}

/* 状态名称用于日志与主机验证，不参与控制判断。 */
const char *state_machine_name(system_state_t state)
{
  switch (state) {
    case ST_SAFE_LOCK:
      return "SAFE_LOCK";
    case ST_CLOSE_RAIN:
      return "CLOSE_RAIN";
    case ST_MANUAL:
      return "MANUAL";
    case ST_VENTILATE:
      return "VENTILATE";
    case ST_AUTO_TRACK:
      return "AUTO_TRACK";
    case ST_IDLE:
      return "IDLE";
    default:
      return "UNKNOWN";
  }
}

/*
 * 对应说明书：4.4 工作状态机
 * 状态机先让 decision_arbitrate 计算候选动作，再处理安全锁存与自动迁移
 * 确认。若候选仍在确认窗口内，保留旧命令与旧状态，确保执行层不会收到
 * 未稳定的模式切换。
 */
void state_machine_run(const env_data_t *env,
                       const manual_cmd_t *manual,
                       const safety_status_t *safe,
                       window_cmd_t *cmd,
                       system_state_t *state)
{
  window_cmd_t candidate_cmd;
  system_state_t prior;
  system_state_t candidate;
  uint32_t now;
  uint8_t candidate_safe;
  uint8_t manual_reset;

  if ((cmd == NULL) || (state == NULL)) {
    return;
  }

  /*
   * 先复制旧命令作为候选初值，尤其保证 IDLE 能保持当前目标角，并让
   * 调用者传入的复位脉冲不会被内部临时结构丢失。
   */
  candidate_cmd = *cmd;
  prior = *state;
  manual_reset = cmd->manual_reset;
  candidate = prior;
  now = (env == NULL) ? hal_get_ms() : env->timestamp_ms;

  decision_arbitrate(env, manual, safe, &candidate_cmd, &candidate);
  candidate_cmd.manual_reset = manual_reset;
  candidate_safe = (candidate == ST_SAFE_LOCK) ? 1u : 0u;

  /*
   * 任一安全条件出现即立即锁存；即使后续环境恢复正常，也保持
   * ST_SAFE_LOCK 直到复位脉冲或安全超时。
   */
  if (candidate_safe != 0u) {
    if (g_safe_latched == 0u) {
      g_safe_latched = 1u;
      g_safe_latched_since_ms = now;
    }
    g_pending_valid = 0u;
    *state = ST_SAFE_LOCK;
    candidate_cmd.manual_reset = manual_reset;
    *cmd = candidate_cmd;
    return;
  }

  /*
   * 当前仍处于安全锁，先检查复位条件。未获准释放时强制安全动作，
   * 不让 safety_status 已被滤波清除的瞬间提前恢复追光或通风。
   */
  if (g_safe_latched != 0u) {
    if (state_can_release_safe(safe, now, manual_reset) == 0u) {
      g_pending_valid = 0u;
      candidate_cmd.valid = 1u;
      candidate_cmd.target_main_angle_deg = SAFE_ANGLE;
      candidate_cmd.sub_window_open = 0u;
      candidate_cmd.safety_lock = 1u;
      candidate_cmd.manual_reset = manual_reset;
      *state = ST_SAFE_LOCK;
      *cmd = candidate_cmd;
      return;
    }
    g_safe_latched = 0u;
  }

  /*
   * 安全、雨天、手动属于保护或人工动作，立即提交。自动通风、追光、
   * 保持需要连续候选一致，防止温度和光照在阈值附近来回切换。
   */
  if ((prior == candidate) || (state_is_immediate(candidate) != 0u) ||
      (state_confirm_candidate(candidate, now) != 0u)) {
    g_pending_valid = 0u;
    *state = candidate;
    *cmd = candidate_cmd;
    return;
  }

  /*
   * 候选尚未稳定：保留原状态和原命令，但下一控制周期继续观察。
   * 若原状态是安全状态，上面的分支已所有路径拦截，不会到达这里。
   */
  cmd->manual_reset = manual_reset;
}
