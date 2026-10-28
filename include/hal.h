/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：hal.h
 * 模块说明：声明平台无关硬件访问层及主机模拟接口
 * 对应说明书章节：3.4 硬件抽象层
 */
#ifndef SMART_WINDOW_HAL_H
#define SMART_WINDOW_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 所有嵌入式硬件访问必须经过本层。核心模块不直接引用 MCU 厂商头文件，
 * 因此同一套业务代码可在 STM32、其他 MCU 或主机桩环境编译运行。
 */
int hal_adc_read(uint8_t channel);
void hal_pwm_set(uint8_t channel, float duty);
int hal_encoder_read(void);
void hal_gpio_write(uint8_t pin, uint8_t value);
uint8_t hal_gpio_read(uint8_t pin);
uint32_t hal_get_ms(void);
void hal_motor_stop(void);
void hal_delay_ms(uint32_t ms);
void hal_delay_us(uint32_t microseconds);

/*
 * 关闭/恢复可抢占当前控制上下文的本地中断。真实移植必须保存原中断状态
 * 并支持嵌套；临界区仅用于复制共享结构体或更新短状态，不得执行延时。
 */
void hal_critical_enter(void);
void hal_critical_exit(void);

/*
 * 主机模拟器扩展接口。这些函数只由 hal_stub.c 实现，产品固件若连接真实
 * HAL 可不提供；测试和 host 演示程序通过它们构造环境与故障。
 */
void hal_reset_mock(void);
void hal_set_mock_adc(uint8_t channel, int millivolts);
void hal_set_mock_encoder(int counts);
void hal_set_mock_gpio(uint8_t pin, uint8_t value);
void hal_set_mock_ms(uint32_t milliseconds);

int hal_mock_get_adc(uint8_t channel);
float hal_mock_get_pwm(uint8_t channel);
int hal_mock_get_encoder(void);
uint8_t hal_mock_get_gpio(uint8_t pin);
uint32_t hal_mock_get_ms(void);
uint32_t hal_mock_get_stop_count(void);
uint32_t hal_mock_get_delay_total_ms(void);
uint32_t hal_mock_get_critical_enter_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_WINDOW_HAL_H */
