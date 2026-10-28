/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：app.c
 * 模块说明：编排采样滤波、决策、执行、通信解析和状态上传
 * 对应说明书章节：3.3 软件运行架构
 */
#include "config.h"
#include "app.h"
#include "decision.h"
#include "filter.h"
#include "hal.h"
#include "main_window.h"
#include "motor.h"
#include "safety.h"
#include "sensor.h"
#include "state_machine.h"
#include "sub_window.h"

#include <math.h>
#include <string.h>

/*
 * 七路环境信号各自拥有移动平均与中值窗口。所有数组长度在编译期固定，
 * 不产生堆分配，也允许静态分析工具验证最大栈和内存占用边界。
 */
#define APP_ENV_SIGNAL_COUNT (7u)

static env_data_t g_env_raw;
static env_data_t g_env_filtered;
static manual_cmd_t g_manual_command;
static safety_status_t g_safety;
static window_cmd_t g_command;
static system_state_t g_state;

static float g_moving_buffers[APP_ENV_SIGNAL_COUNT][FILTER_MOVING_WINDOW];
static float g_median_buffers[APP_ENV_SIGNAL_COUNT][FILTER_MEDIAN_WINDOW];

static status_snapshot_t g_status_history[STATUS_HISTORY_DEPTH];
static uint32_t g_status_upload_count;
static uint32_t g_last_status_upload_ms;
static uint32_t g_timer_tick_count;

/* 返回指定信号在滤波窗口数组中的索引。 */
enum {
  APP_SIGNAL_TEMPERATURE = 0,
  APP_SIGNAL_HUMIDITY,
  APP_SIGNAL_WIND,
  APP_SIGNAL_RAIN,
  APP_SIGNAL_LIGHT_LEFT,
  APP_SIGNAL_LIGHT_RIGHT,
  APP_SIGNAL_LIGHT_UP_DOWN
};

/* CRC-8/ATM 多项式 0x07，初值 0。算法简单且适合短帧。 */
static uint8_t app_crc8(const uint8_t *data, uint16_t length)
{
  uint16_t index;
  uint8_t bit;
  uint8_t crc = 0u;

  if (data == NULL) {
    return 0u;
  }

  for (index = 0u; index < length; ++index) {
    crc ^= data[index];
    for (bit = 0u; bit < 8u; ++bit) {
      if ((crc & 0x80u) != 0u) {
        crc = (uint8_t)(((uint32_t)crc << 1u) ^
                        (uint32_t)MANUAL_CRC_POLYNOMIAL);
      } else {
        crc = (uint8_t)((uint32_t)crc << 1u);
      }
    }
  }
  return crc;
}

/* 小端字节流恢复 float，使用 memcpy 避免严格别名问题。 */
static float app_decode_float_le(const uint8_t *bytes)
{
  uint8_t little_endian[sizeof(float)];
  uint32_t value;
  float result;

  little_endian[0] = bytes[0];
  little_endian[1] = bytes[1];
  little_endian[2] = bytes[2];
  little_endian[3] = bytes[3];

  /*
   * 目标平台按 IEEE-754 单精度运行；如果 sizeof(float) 不是 4，
   * 帧格式不匹配，返回 NaN 让上层拒绝命令。
   */
  if (sizeof(float) != 4u) {
    return NAN;
  }

  value = ((uint32_t)little_endian[0]) |
          ((uint32_t)little_endian[1] << 8u) |
          ((uint32_t)little_endian[2] << 16u) |
          ((uint32_t)little_endian[3] << 24u);
  memcpy(&result, &value, sizeof(result));
  return result;
}

/*
 * 对应说明书：4.8.2 手动命令解析
 * 只有长度、帧头、CRC 全部通过才更新时间戳和命令。复位标志同样仅在
 * 完整帧校验后登记，防止线路噪声触发安全复位。
 */
void manual_cmd_parse(const uint8_t *buf, uint16_t len, manual_cmd_t *out)
{
  uint8_t expected_crc;
  float angle;
  uint8_t flags;

  if (out == NULL) {
    return;
  }

  out->valid = 0u;
  out->manual_angle_deg = SAFE_ANGLE;
  out->manual_sub_open = 0u;
  out->timestamp_ms = hal_get_ms();

  if ((buf == NULL) || (len != MANUAL_FRAME_LENGTH)) {
    return;
  }
  if (buf[0] != MANUAL_FRAME_HEADER) {
    return;
  }

  expected_crc = app_crc8(&buf[2], (uint16_t)(MANUAL_FRAME_LENGTH - 2u));
  if (expected_crc != buf[MANUAL_FRAME_CRC_INDEX]) {
    return;
  }

  angle = app_decode_float_le(&buf[2]);
  flags = buf[6];
  if (!isfinite(angle) ||
      (angle < MAIN_ANGLE_MIN) ||
      (angle > MAIN_ANGLE_MAX)) {
    return;
  }

  out->manual_angle_deg = angle;
  out->manual_sub_open = (uint8_t)(flags & MANUAL_FLAG_SUB_OPEN);
  out->timestamp_ms = hal_get_ms();
  out->valid = 1u;

  if ((flags & MANUAL_FLAG_RESET) != 0u) {
    safety_request_manual_reset();
  }
}

/*
 * 对应说明书：4.2.3 双滤波融合
 * 移动平均抑制连续高斯噪声，中值抑制孤立尖峰。两路独立窗口取均值，
 * 兼顾平滑性和异常值剔除。
 */
static float app_filter_signal(uint8_t signal_index, float raw)
{
  float average;
  float median;

  if (signal_index >= APP_ENV_SIGNAL_COUNT) {
    return 0.0f;
  }
  if (!isfinite(raw)) {
    return raw;
  }

  average = moving_average_filter(raw,
                                  g_moving_buffers[signal_index],
                                  (uint8_t)FILTER_MOVING_WINDOW);
  median = median_filter(raw,
                         g_median_buffers[signal_index],
                         (uint8_t)FILTER_MEDIAN_WINDOW);
  return (FILTER_AVERAGE_WEIGHT * average) +
         (FILTER_MEDIAN_WEIGHT * median);
}

/* 初始化单个信号的两个历史窗口，避免上电零值导致长时间输出偏低。 */
static void app_seed_filter(uint8_t signal_index, float value)
{
  if (signal_index >= APP_ENV_SIGNAL_COUNT) {
    return;
  }
  filter_fill(g_moving_buffers[signal_index],
              (uint8_t)FILTER_MOVING_WINDOW,
              value);
  filter_fill(g_median_buffers[signal_index],
              (uint8_t)FILTER_MEDIAN_WINDOW,
              value);
}

/* 保存原始有效位，同时更新七个滤波通道。 */
static void app_filter_environment(const env_data_t *raw, env_data_t *filtered)
{
  float vertical_light;

  if ((raw == NULL) || (filtered == NULL)) {
    return;
  }

  filtered->temperature_c =
      app_filter_signal(APP_SIGNAL_TEMPERATURE, raw->temperature_c);
  filtered->humidity_pct =
      app_filter_signal(APP_SIGNAL_HUMIDITY, raw->humidity_pct);
  filtered->wind_speed_mps =
      app_filter_signal(APP_SIGNAL_WIND, raw->wind_speed_mps);
  filtered->rain_level =
      app_filter_signal(APP_SIGNAL_RAIN, raw->rain_level);
  filtered->light_left =
      app_filter_signal(APP_SIGNAL_LIGHT_LEFT, raw->light_left);
  filtered->light_right =
      app_filter_signal(APP_SIGNAL_LIGHT_RIGHT, raw->light_right);

  /*
   * 上、下光照共同合成一个滤波通道，保持垂直差判断稳定。输出时把同值
   * 分配给上下字段，避免两个独立窗口因采样相位产生伪差异。
   */
  vertical_light = 0.5f * (raw->light_up + raw->light_down);
  if (!isfinite(vertical_light)) {
    vertical_light = raw->light_up;
  }
  vertical_light =
      app_filter_signal(APP_SIGNAL_LIGHT_UP_DOWN, vertical_light);
  filtered->light_up = vertical_light;
  filtered->light_down = vertical_light;

  filtered->timestamp_ms = raw->timestamp_ms;
  filtered->valid_mask = raw->valid_mask;
}

/* 在短临界区中复制完整命令，定时器不会看到半更新结构体。 */
static window_cmd_t app_read_command_snapshot(void)
{
  window_cmd_t snapshot;

  hal_critical_enter();
  snapshot = g_command;
  hal_critical_exit();
  return snapshot;
}

/* 主循环在临界区内提交完整命令，定时器只能看到旧版或新版。 */
static void app_write_command_snapshot(const window_cmd_t *command)
{
  if (command == NULL) {
    return;
  }
  hal_critical_enter();
  g_command = *command;
  hal_critical_exit();
}

/* 手动命令可能在通信中断中生成，用同一临界区复制完整快照。 */
static manual_cmd_t app_read_manual_command_snapshot(void)
{
  manual_cmd_t snapshot;

  hal_critical_enter();
  snapshot = g_manual_command;
  hal_critical_exit();
  return snapshot;
}

/*
 * 对应说明书：4.5.6 定时器控制回调
 * 定时器回调承担采样、滤波、主窗反馈读取、PID 和 PWM 更新；不做副窗
 * 行程循环和通信发送等可能阻塞的工作。
 */
void app_timer_tick(void)
{
  window_cmd_t command_snapshot;
  env_data_t filtered_snapshot;
  float current_angle;
  float pwm_output;

  ++g_timer_tick_count;
  env_sensor_read(&g_env_raw);
  app_filter_environment(&g_env_raw, &filtered_snapshot);
  hal_critical_enter();
  g_env_filtered = filtered_snapshot;
  hal_critical_exit();

  command_snapshot = app_read_command_snapshot();
  current_angle = motor_read_main_angle_deg();
  main_window_angle_ctrl(command_snapshot.target_main_angle_deg,
                         current_angle,
                         &pwm_output);
}

/* 把累计命令发送到安全位置，避免上电时沿用未初始化目标角。 */
static void app_install_safe_command(void)
{
  g_command.valid = 1u;
  g_command.target_main_angle_deg = SAFE_ANGLE;
  g_command.sub_window_open = 0u;
  g_command.safety_lock = 1u;
  g_command.manual_reset = 0u;
  app_write_command_snapshot(&g_command);
}

/* 在短临界区中取得完整的滤波环境快照。 */
static env_data_t app_read_environment_snapshot(void)
{
  env_data_t snapshot;

  hal_critical_enter();
  snapshot = g_env_filtered;
  hal_critical_exit();
  return snapshot;
}

/*
 * 对应说明书：3.3 软件运行架构
 * 初始化顺序保证安全状态对象先建立绑定，再允许电机和状态机启动。
 */
void app_init(void)
{
  float initial_vertical_light;

  memset(&g_env_raw, 0, sizeof(g_env_raw));
  memset(&g_env_filtered, 0, sizeof(g_env_filtered));
  memset(&g_manual_command, 0, sizeof(g_manual_command));
  memset(g_moving_buffers, 0, sizeof(g_moving_buffers));
  memset(g_median_buffers, 0, sizeof(g_median_buffers));
  memset(g_status_history, 0, sizeof(g_status_history));
  g_status_upload_count = 0u;
  g_last_status_upload_ms = hal_get_ms();
  g_timer_tick_count = 0u;

  sensor_init();
  safety_init(&g_safety);
  motor_init();
  motor_bind_safety_status(&g_safety);
  main_window_init();
  sub_window_bind_safety_status(&g_safety);
  sub_window_init();
  state_machine_reset();
  g_state = ST_IDLE;
  app_install_safe_command();

  /*
   * 上电先读取一次传感器作为滤波种子。若现场确实无数据，raw 的有效位
   * 会令安全层进入传感器故障分支，而不是用零值假称舒适环境。
   */
  env_sensor_read(&g_env_raw);
  app_filter_environment(&g_env_raw, &g_env_filtered);
  app_seed_filter(APP_SIGNAL_TEMPERATURE, g_env_filtered.temperature_c);
  app_seed_filter(APP_SIGNAL_HUMIDITY, g_env_filtered.humidity_pct);
  app_seed_filter(APP_SIGNAL_WIND, g_env_filtered.wind_speed_mps);
  app_seed_filter(APP_SIGNAL_RAIN, g_env_filtered.rain_level);
  app_seed_filter(APP_SIGNAL_LIGHT_LEFT, g_env_filtered.light_left);
  app_seed_filter(APP_SIGNAL_LIGHT_RIGHT, g_env_filtered.light_right);
  initial_vertical_light =
      0.5f * (g_env_filtered.light_up + g_env_filtered.light_down);
  app_seed_filter(APP_SIGNAL_LIGHT_UP_DOWN, initial_vertical_light);
}

/* 复位顺序：先清除软件锁，再重新识别限位，最后允许状态机本周期释放。 */
static void app_execute_manual_reset(void)
{
  safety_clear_latches(&g_safety);
  motor_clear_stall();
  sub_window_reset();
}

/*
 * 对应说明书：3.3.2 主循环
 * 主循环只处理模式和副窗决策，不忙等行程，也不执行可能长达数毫秒的
 * 采样滤波。定时器回调单独维护控制周期，满足裸机分层要求。
 */
void app_run(void)
{
  env_data_t env_snapshot;
  manual_cmd_t manual_snapshot;
  window_cmd_t command_work;
  uint8_t reset_requested;
  uint32_t now;

  reset_requested = safety_consume_manual_reset();
  if (reset_requested != 0u) {
    app_execute_manual_reset();
  }

  env_snapshot = app_read_environment_snapshot();
  manual_snapshot = app_read_manual_command_snapshot();
  safety_override(&env_snapshot, &g_safety);

  command_work = g_command;
  command_work.manual_reset = reset_requested;
  state_machine_run(&env_snapshot,
                    &manual_snapshot,
                    &g_safety,
                    &command_work,
                    &g_state);
  g_command = command_work;
  app_write_command_snapshot(&g_command);

  /*
   * 副窗在安全或雨天分支关闭，其余分支由仲裁命令决定。打开和关闭都是
   * 非阻塞启动，后续限位/超时/堵转由 sub_window_service 推进。
   */
  if (command_work.sub_window_open != 0u) {
    sub_window_open();
  } else {
    sub_window_close();
  }
  sub_window_service();

  now = hal_get_ms();
  if ((now - g_last_status_upload_ms) >= STATUS_UPLOAD_MS) {
    status_upload(&env_snapshot, &command_work, g_state);
    g_last_status_upload_ms = now;
  }
}

/*
 * 对应说明书：4.8.4 状态上传
 * 在裸机目标板上，本快照可交给 UART DMA；主机测试将其保存到固定环形
 * 历史数组。函数不阻塞发送、不使用 malloc，也不引入第三方协议库。
 */
void status_upload(const env_data_t *env,
                   const window_cmd_t *cmd,
                   system_state_t state)
{
  status_snapshot_t snapshot;
  uint32_t index;

  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.timestamp_ms = hal_get_ms();
  snapshot.state = state;

  if (env != NULL) {
    snapshot.timestamp_ms = env->timestamp_ms;
    snapshot.wind_speed_mps = env->wind_speed_mps;
    snapshot.rain_level = env->rain_level;
    snapshot.valid_mask = env->valid_mask;
  }
  if (cmd != NULL) {
    snapshot.target_main_angle_deg = cmd->target_main_angle_deg;
    snapshot.sub_window_open = cmd->sub_window_open;
    snapshot.safety_lock = cmd->safety_lock;
  }

  index = g_status_upload_count % STATUS_HISTORY_DEPTH;
  g_status_history[index] = snapshot;
  ++g_status_upload_count;
}

/* 返回成功生成的上传快照总数，支持自然回绕。 */
uint32_t status_get_upload_count(void)
{
  return g_status_upload_count;
}

/* 获取最新快照；尚未上传时返回固定数组中的零初始化条目。 */
const status_snapshot_t *status_get_latest(void)
{
  uint32_t index;

  if (g_status_upload_count == 0u) {
    return &g_status_history[0];
  }
  index = (g_status_upload_count - 1u) % STATUS_HISTORY_DEPTH;
  return &g_status_history[index];
}

/*
 * 测试与应用监控使用的只读入口。由于固定文件树未包含 app.h，这些
 * 应用级诊断函数在 app.c 中实现，并由 main/test 以最小外部声明使用。
 */
uint32_t app_get_timer_tick_count(void)
{
  return g_timer_tick_count;
}

const env_data_t *app_get_filtered_environment(void)
{
  return &g_env_filtered;
}

const safety_status_t *app_get_safety_status(void)
{
  return &g_safety;
}

const window_cmd_t *app_get_command(void)
{
  return &g_command;
}

system_state_t app_get_state(void)
{
  return g_state;
}

void app_set_manual_command(const manual_cmd_t *manual)
{
  hal_critical_enter();
  if (manual == NULL) {
    memset(&g_manual_command, 0, sizeof(g_manual_command));
  } else {
    g_manual_command = *manual;
  }
  hal_critical_exit();
}
