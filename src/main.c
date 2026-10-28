/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：main.c
 * 模块说明：提供目标板定时器入口说明和主机演示入口
 * 对应说明书章节：3.3 软件运行架构
 */
#include "config.h"
#include "decision.h"
#include "hal.h"
#include "state_machine.h"

#ifdef HOST_BUILD
#include <stdio.h>
#endif

/* app.h 不在固定文件树中，应用调度入口在此做最小外部声明。 */
void app_init(void);
void app_run(void);
void app_timer_tick(void);
const env_data_t *app_get_filtered_environment(void);
const window_cmd_t *app_get_command(void);
system_state_t app_get_state(void);
uint32_t app_get_timer_tick_count(void);

/*
 * 目标板定时器回调伪代码对应实现：真实工程把本函数挂到 100 ms 周期
 * 定时中断；主机演示用主循环按照 SAMPLE_PERIOD_MS 显式调用。
 */
static void hardware_timer_callback(void)
{
  app_timer_tick();
}

#ifdef HOST_BUILD
/*
 * 主机演示使用测试桩产生一组舒适且左强右弱的光照，运行约四秒，展示
 * 追光仲裁、控制周期和状态上传。它不改变核心模块的目标板依赖边界。
 */
static void main_configure_demo_inputs(void)
{
  hal_reset_mock();
  hal_set_mock_adc(SENSOR_ADC_TEMPERATURE, 230);
  hal_set_mock_adc(SENSOR_ADC_HUMIDITY, 550);
  hal_set_mock_adc(SENSOR_ADC_WIND, 60);
  hal_set_mock_adc(SENSOR_ADC_RAIN, 10);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_LEFT, 900);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_RIGHT, 300);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_UP, 800);
  hal_set_mock_adc(SENSOR_ADC_LIGHT_DOWN, 500);
  hal_set_mock_adc(ADC_CHANNEL_MOTOR_CURRENT, 10);
  hal_set_mock_gpio(LIMIT_PIN_SUB_CLOSED, 1u);
  hal_set_mock_gpio(LIMIT_PIN_SUB_OPEN, 0u);
}

int main(void)
{
  uint32_t elapsed_ms;
  uint32_t iteration;
  const env_data_t *environment;
  const window_cmd_t *command;

  main_configure_demo_inputs();
  app_init();
  elapsed_ms = hal_get_ms();

  for (iteration = 0u; iteration < 4000u; ++iteration) {
    if ((hal_get_ms() - elapsed_ms) >= SAMPLE_PERIOD_MS) {
      hardware_timer_callback();
      elapsed_ms = hal_get_ms();
    }
    app_run();
    hal_delay_ms(1u);
  }

  environment = app_get_filtered_environment();
  command = app_get_command();
  printf("Smart window host demo completed\n");
  printf("state=%s angle_target=%.2f wind=%.2f uploads=%lu\n",
         state_machine_name(app_get_state()),
         (double)command->target_main_angle_deg,
         (double)environment->wind_speed_mps,
         (unsigned long)status_get_upload_count());
  printf("timer_ticks=%lu simulated_ms=%lu\n",
         (unsigned long)app_get_timer_tick_count(),
         (unsigned long)hal_get_ms());
  return 0;
}
#else
/*
 * 裸机入口通常由启动文件直接调用 C 运行时，不应进入无限主循环之外的
 * 主机逻辑。保留一个永不返回的调度函数供集成工程替换硬件初始化。
 */
int main(void)
{
  app_init();
  for (;;) {
    app_run();
  }
}
#endif
