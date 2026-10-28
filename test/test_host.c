/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：test_host.c
 * 模块说明：验证优先级仲裁、保护动作、闭环限位和异常路径
 * 对应说明书章节：10.2 主机自动化测试
 */
#include "config.h"
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
#include <stdio.h>
#include <string.h>

/*
 * 固定文件树没有 app.h，测试对应用诊断入口作最小外部声明。
 * 这些入口只读，不参与核心控制逻辑。
 */
void app_init(void);
void app_run(void);
void app_timer_tick(void);
void app_set_manual_command(const manual_cmd_t *manual);
const env_data_t *app_get_filtered_environment(void);
const safety_status_t *app_get_safety_status(void);
const window_cmd_t *app_get_command(void);
system_state_t app_get_state(void);
uint32_t app_get_timer_tick_count(void);

static unsigned int g_checks;
static unsigned int g_failures;

#define CHECK_TRUE(condition)                                                \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(condition)) {                                                      \
      ++g_failures;                                                          \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                              \
  do {                                                                       \
    double check_actual = (double)(actual);                                  \
    double check_expected = (double)(expected);                              \
    double check_tolerance = (double)(tolerance);                            \
    ++g_checks;                                                              \
    if (fabs(check_actual - check_expected) > check_tolerance) {             \
      ++g_failures;                                                          \
      printf("FAIL %s:%d: got %.4f expected %.4f\n",                        \
             __FILE__, __LINE__, check_actual, check_expected);             \
    }                                                                        \
  } while (0)

/* 构造所有通道有效的舒适环境。 */
static env_data_t test_make_environment(void)
{
  env_data_t env;

  memset(&env, 0, sizeof(env));
  env.temperature_c = 23.0f;
  env.humidity_pct = 55.0f;
  env.wind_speed_mps = 2.0f;
  env.rain_level = 0.0f;
  env.light_left = 80.0f;
  env.light_right = 80.0f;
  env.light_up = 70.0f;
  env.light_down = 70.0f;
  env.timestamp_ms = hal_get_ms();
  env.valid_mask = SENSOR_VALID_ALL;
  return env;
}

/* 清空所有模块的静态状态，确保每个测试彼此独立。 */
static void test_reset_modules(void)
{
  hal_reset_mock();
  sensor_init();
  main_window_init();
  motor_init();
  sub_window_init();
  state_machine_reset();
}

/* 对应测试：正常追光。 */
static void test_auto_tracking(void)
{
  env_data_t env;
  manual_cmd_t manual;
  safety_status_t safety;
  window_cmd_t command;
  system_state_t state = ST_IDLE;

  test_reset_modules();
  safety_init(&safety);
  memset(&manual, 0, sizeof(manual));
  memset(&command, 0, sizeof(command));
  command.target_main_angle_deg = 5.0f;

  env = test_make_environment();
  env.light_left = 1.0f;
  env.light_right = 0.1f;
  env.timestamp_ms = 0u;

  decision_arbitrate(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_AUTO_TRACK);
  CHECK_NEAR(command.target_main_angle_deg, 40.5f, 0.01f);
  CHECK_TRUE(command.sub_window_open == 0u);

  /* 垂直光差也可触发追光，但目标角仍严格按水平差计算。 */
  env.light_left = 0.30f;
  env.light_right = 0.28f;
  env.light_up = 0.9f;
  env.light_down = 0.1f;
  decision_arbitrate(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_AUTO_TRACK);
  CHECK_NEAR(command.target_main_angle_deg, 0.9f, 0.01f);
}

/* 对应测试：风速锁定。 */
static void test_wind_lock(void)
{
  env_data_t env;
  manual_cmd_t manual;
  safety_status_t safety;
  window_cmd_t command;
  system_state_t state = ST_IDLE;

  test_reset_modules();
  safety_init(&safety);
  memset(&manual, 0, sizeof(manual));
  memset(&command, 0, sizeof(command));

  env = test_make_environment();
  env.wind_speed_mps = WIND_LOCK_MPS + 0.5f;
  env.timestamp_ms = hal_get_ms();
  safety_override(&env, &safety);

  CHECK_TRUE(safety.wind_lock != 0u);
  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_SAFE_LOCK);
  CHECK_NEAR(command.target_main_angle_deg, SAFE_ANGLE, 0.001f);
  CHECK_TRUE(command.sub_window_open == 0u);
  CHECK_TRUE(command.safety_lock != 0u);

  /* 滞回内侧仍锁定，回落到锁定值减迟滞后才清除风锁标志。 */
  env.wind_speed_mps = WIND_LOCK_MPS - (WIND_HYST * 0.5f);
  safety_override(&env, &safety);
  CHECK_TRUE(safety.wind_lock != 0u);
  env.wind_speed_mps = WIND_LOCK_MPS - WIND_HYST;
  safety_override(&env, &safety);
  CHECK_TRUE(safety.wind_lock == 0u);
}

/* 补充测试：安全条件先恢复时，仍须人工复位才可退出安全锁。 */
static void test_safety_latch_and_reset(void)
{
  env_data_t env;
  manual_cmd_t manual;
  safety_status_t safety;
  window_cmd_t command;
  system_state_t state = ST_IDLE;
  uint8_t reset;

  test_reset_modules();
  safety_init(&safety);
  memset(&manual, 0, sizeof(manual));
  memset(&command, 0, sizeof(command));

  env = test_make_environment();
  env.wind_speed_mps = WIND_LOCK_MPS + 1.0f;
  env.timestamp_ms = 0u;
  safety_override(&env, &safety);
  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_SAFE_LOCK);

  env.wind_speed_mps = 1.0f;
  hal_set_mock_ms(100u);
  env.timestamp_ms = 100u;
  safety_override(&env, &safety);
  CHECK_TRUE(safety.wind_lock == 0u);
  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_SAFE_LOCK);
  CHECK_TRUE(state_machine_safe_is_latched() != 0u);

  safety_request_manual_reset();
  reset = safety_consume_manual_reset();
  CHECK_TRUE(reset != 0u);
  command.manual_reset = reset;
  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state_machine_safe_is_latched() == 0u);

  hal_set_mock_ms(500u);
  env.timestamp_ms = 500u;
  command.manual_reset = 0u;
  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_IDLE);
}

/* 对应测试：雨滴关窗。 */
static void test_rain_close(void)
{
  env_data_t env;
  manual_cmd_t manual;
  safety_status_t safety;
  window_cmd_t command;
  system_state_t state = ST_IDLE;

  test_reset_modules();
  safety_init(&safety);
  memset(&manual, 0, sizeof(manual));
  memset(&command, 0, sizeof(command));
  command.target_main_angle_deg = 20.0f;

  env = test_make_environment();
  env.rain_level = RAIN_DETECT + 0.05f;
  env.timestamp_ms = hal_get_ms();
  safety_override(&env, &safety);

  /* 0.25 未达到 0.8 雨量锁，不能误报整机安全锁。 */
  CHECK_TRUE(safety.rain_lock == 0u);
  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_CLOSE_RAIN);
  CHECK_NEAR(command.target_main_angle_deg, RAIN_ANGLE, 0.001f);
  CHECK_TRUE(command.sub_window_open == 0u);
}

/* 对应测试：手动优先。 */
static void test_manual_priority(void)
{
  env_data_t env;
  manual_cmd_t manual;
  safety_status_t safety;
  window_cmd_t command;
  system_state_t state = ST_IDLE;

  test_reset_modules();
  safety_init(&safety);
  memset(&command, 0, sizeof(command));
  env = test_make_environment();
  env.timestamp_ms = 1000u;

  manual.valid = 1u;
  manual.manual_angle_deg = 47.0f;
  manual.manual_sub_open = 1u;
  manual.timestamp_ms = 1000u;

  state_machine_run(&env, &manual, &safety, &command, &state);
  CHECK_TRUE(state == ST_MANUAL);
  CHECK_NEAR(command.target_main_angle_deg, 47.0f, 0.001f);
  CHECK_TRUE(command.sub_window_open != 0u);
}

/* 对应测试：手动超时回自动。 */
static void test_manual_timeout(void)
{
  env_data_t env;
  manual_cmd_t manual;

  memset(&manual, 0, sizeof(manual));
  manual.valid = 1u;
  manual.manual_angle_deg = 35.0f;
  manual.manual_sub_open = 1u;
  manual.timestamp_ms = 100u;

  env = test_make_environment();
  env.timestamp_ms = 100u + MANUAL_TIMEOUT_MS;
  CHECK_TRUE(decision_manual_is_valid(&env, &manual) != 0u);
  env.timestamp_ms = 101u + MANUAL_TIMEOUT_MS;
  CHECK_TRUE(decision_manual_is_valid(&env, &manual) == 0u);

  env.light_left = 1.0f;
  env.light_right = 0.2f;
  {
    safety_status_t safety;
    window_cmd_t command;
    system_state_t state = ST_MANUAL;

    safety_init(&safety);
    memset(&command, 0, sizeof(command));
    decision_arbitrate(&env, &manual, &safety, &command, &state);
    CHECK_TRUE(state == ST_AUTO_TRACK);
    CHECK_TRUE(command.sub_window_open == 0u);
  }
}

/* 对应测试：限位停电机。 */
static void test_main_limit_stop(void)
{
  float pwm = 1.0f;
  uint32_t stops_before;

  test_reset_modules();
  stops_before = hal_mock_get_stop_count();
  main_window_angle_ctrl(30.0f, MAIN_ANGLE_MAX + 0.1f, &pwm);
  CHECK_NEAR(pwm, 0.0f, 0.001f);
  CHECK_TRUE(hal_mock_get_stop_count() > stops_before);
  CHECK_NEAR(hal_mock_get_pwm(HAL_PWM_MAIN_CHANNEL), 0.0f, 0.001f);

  CHECK_TRUE(limit_check(MAIN_ANGLE_MIN) != 0u);
  CHECK_TRUE(limit_check(MAIN_ANGLE_MAX) != 0u);
  CHECK_TRUE(limit_check(NAN) == 0u);
}

/* 对应测试：副窗电流堵转。 */
static void test_sub_window_stall(void)
{
  safety_status_t safety;

  test_reset_modules();
  safety_init(&safety);
  motor_bind_safety_status(&safety);
  sub_window_bind_safety_status(&safety);

  /* 限位指示副窗当前打开，随后请求关闭，建立一次有效运动。 */
  hal_set_mock_gpio(LIMIT_PIN_SUB_CLOSED, 0u);
  hal_set_mock_gpio(LIMIT_PIN_SUB_OPEN, 1u);
  sub_window_init();
  hal_set_mock_adc(ADC_CHANNEL_MOTOR_CURRENT, 100);
  sub_window_close();
  CHECK_TRUE(sub_window_is_moving() != 0u);
  sub_window_service();

  CHECK_TRUE(safety.motor_stall != 0u);
  CHECK_TRUE(motor_has_stall() != 0u);
  CHECK_TRUE(sub_window_get_state() == SUB_WINDOW_FAULT);
  CHECK_NEAR(hal_mock_get_pwm(HAL_PWM_SUB_CHANNEL), 0.0f, 0.001f);
}

/* 补充测试：副窗行程超时。 */
static void test_sub_window_timeout(void)
{
  safety_status_t safety;
  sub_window_state_t state;

  test_reset_modules();
  safety_init(&safety);
  motor_bind_safety_status(&safety);
  sub_window_bind_safety_status(&safety);
  hal_set_mock_gpio(LIMIT_PIN_SUB_CLOSED, 0u);
  hal_set_mock_gpio(LIMIT_PIN_SUB_OPEN, 1u);
  sub_window_init();
  hal_set_mock_adc(ADC_CHANNEL_MOTOR_CURRENT, 5);
  sub_window_close();
  hal_delay_ms(SUB_WINDOW_TRAVEL_MS);
  sub_window_service();
  state = sub_window_get_state();

  CHECK_TRUE(state == SUB_WINDOW_FAULT);
  CHECK_TRUE(safety.motor_stall != 0u);
}

/* 补充测试：NaN与越界传感器不进入有效集合。 */
static void test_sensor_validation(void)
{
  env_data_t env;
  uint8_t mask;

  test_reset_modules();
  env = test_make_environment();
  env.timestamp_ms = 12345u;
  env.temperature_c = NAN;
  env.humidity_pct = HUMIDITY_SENSOR_MAX + 1.0f;
  mask = sensor_valid_check(&env);
  CHECK_TRUE((mask & SENSOR_VALID_TEMPERATURE) == 0u);
  CHECK_TRUE((mask & SENSOR_VALID_HUMIDITY) == 0u);
  CHECK_TRUE((mask & SENSOR_VALID_WIND) != 0u);
}

/* 补充测试：移动平均和孤立尖峰中值抑制。 */
static void test_filters(void)
{
  float average_buffer[FILTER_MOVING_WINDOW];
  float median_buffer[FILTER_MEDIAN_WINDOW];
  float output;

  filter_fill(average_buffer,
              (uint8_t)FILTER_MOVING_WINDOW,
              1.0f);
  output = moving_average_filter(3.0f,
                                 average_buffer,
                                 (uint8_t)FILTER_MOVING_WINDOW);
  CHECK_NEAR(output, 1.4f, 0.001f);

  filter_fill(median_buffer,
              (uint8_t)FILTER_MEDIAN_WINDOW,
              10.0f);
  output = median_filter(1000.0f,
                         median_buffer,
                         (uint8_t)FILTER_MEDIAN_WINDOW);
  CHECK_NEAR(output, 10.0f, 0.001f);
  CHECK_NEAR(moving_average_filter(1.0f, NULL, 3u), 0.0f, 0.001f);
}

/* 计算与 app.c 相同 CRC，用于构造合法手动帧。 */
static uint8_t test_crc8(const uint8_t *data, uint16_t length)
{
  uint8_t crc = 0u;
  uint16_t index;
  uint8_t bit;

  for (index = 0u; index < length; ++index) {
    crc ^= data[index];
    for (bit = 0u; bit < 8u; ++bit) {
      crc = ((crc & 0x80u) != 0u) ?
            (uint8_t)(((uint32_t)crc << 1u) ^
                      (uint32_t)MANUAL_CRC_POLYNOMIAL) :
            (uint8_t)((uint32_t)crc << 1u);
    }
  }
  return crc;
}

/* 补充测试：合法CRC手动帧解析和损坏帧拒绝。 */
static void test_manual_frame_parse(void)
{
  uint8_t frame[MANUAL_FRAME_LENGTH];
  manual_cmd_t parsed;
  float angle = 25.0f;
  uint32_t bits;

  memset(frame, 0, sizeof(frame));
  frame[0] = MANUAL_FRAME_HEADER;
  memcpy(&bits, &angle, sizeof(bits));
  frame[2] = (uint8_t)(bits & 0xFFu);
  frame[3] = (uint8_t)((bits >> 8u) & 0xFFu);
  frame[4] = (uint8_t)((bits >> 16u) & 0xFFu);
  frame[5] = (uint8_t)((bits >> 24u) & 0xFFu);
  frame[6] = 0x01u;
  frame[1] = test_crc8(&frame[2],
                       (uint16_t)(MANUAL_FRAME_LENGTH - 2u));

  manual_cmd_parse(frame, MANUAL_FRAME_LENGTH, &parsed);
  CHECK_TRUE(parsed.valid != 0u);
  CHECK_NEAR(parsed.manual_angle_deg, 25.0f, 0.001f);
  CHECK_TRUE(parsed.manual_sub_open != 0u);

  frame[3] ^= 0x40u;
  manual_cmd_parse(frame, MANUAL_FRAME_LENGTH, &parsed);
  CHECK_TRUE(parsed.valid == 0u);
}

/* 补充测试：完整应用链路的采样、滤波、追光和状态上传。 */
static void test_integrated_pipeline(void)
{
  uint32_t step;
  const safety_status_t *safety;

  hal_reset_mock();
  hal_set_mock_adc(SENSOR_ADC_TEMPERATURE, 230);
  hal_set_mock_adc(SENSOR_ADC_HUMIDITY, 550);
  hal_set_mock_adc(SENSOR_ADC_WIND, 50);
  hal_set_mock_adc(SENSOR_ADC_RAIN, 10);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_LEFT, 900);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_RIGHT, 200);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_UP, 700);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_DOWN, 600);
  hal_set_mock_adc(ADC_CHANNEL_MOTOR_CURRENT, 10);
  hal_set_mock_gpio(LIMIT_PIN_SUB_CLOSED, 1u);

  app_init();
  for (step = 0u; step < 8u; ++step) {
    app_timer_tick();
    app_run();
    hal_delay_ms(SAMPLE_PERIOD_MS);
  }

  safety = app_get_safety_status();
  CHECK_TRUE(safety_any_lock(safety) == 0u);
  CHECK_TRUE(app_get_timer_tick_count() == 8u);
  CHECK_TRUE(status_get_upload_count() > 0u);
  CHECK_TRUE(app_get_state() == ST_AUTO_TRACK);
  CHECK_TRUE(app_get_command()->target_main_angle_deg > 0.0f);
}

/* 主测试入口按独立场景执行，任一失败仍继续收集全部证据。 */
int main(void)
{
  test_auto_tracking();
  test_wind_lock();
  test_safety_latch_and_reset();
  test_rain_close();
  test_manual_priority();
  test_manual_timeout();
  test_main_limit_stop();
  test_sub_window_stall();
  test_sub_window_timeout();
  test_sensor_validation();
  test_filters();
  test_manual_frame_parse();
  test_integrated_pipeline();

  printf("test_host: %u checks, %u failures\n", g_checks, g_failures);
  return (g_failures == 0u) ? 0 : 1;
}
