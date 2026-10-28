/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：config.h
 * 模块说明：集中定义环境阈值、控制参数、HAL映射和系统容量
 * 对应说明书章节：5.1 参数配置表
 */
#ifndef SMART_WINDOW_CONFIG_H
#define SMART_WINDOW_CONFIG_H

/*
 * 本文件是产品参数唯一入口。控制算法只引用这些符号，不在实现文件内写
 * 可调阈值。括号和 f 后缀同时保证表达式可安全组合，并保持 C99 浮点类型。
 */

/* 温度舒适区与迟滞，单位摄氏度。 */
#define TEMP_COMFORT_MIN       (16.0f)
#define TEMP_COMFORT_MAX       (28.0f)
#define TEMP_HYST              (1.0f)

/* 湿度舒适区与迟滞，单位百分比。 */
#define HUMIDITY_COMFORT_MIN   (40.0f)
#define HUMIDITY_COMFORT_MAX   (70.0f)
#define HUM_HYST               (3.0f)

/* 风速保护，单位米每秒。正常与锁定之间形成缓冲带。 */
#define WIND_SAFE_MPS          (8.0f)
#define WIND_LOCK_MPS          (12.0f)
#define WIND_HYST              (1.0f)

/* 雨量归一化阈值，0 表示无雨，1 表示传感器满量程。 */
#define RAIN_DETECT            (0.2f)
#define RAIN_LOCK              (0.8f)
#define RAIN_HYST              (0.05f)

/* 光照差和角度死区。光照数据使用归一化相对值，范围由传感器模块给定。 */
#define LIGHT_DEADBAND         (0.05f)
#define ANGLE_DEADBAND         (1.5f)

/* 主窗机械角度以及各模式目标角，单位度。 */
#define MAIN_ANGLE_MIN         (-90.0f)
#define MAIN_ANGLE_MAX         (90.0f)
#define SAFE_ANGLE             (0.0f)
#define RAIN_ANGLE             (0.0f)
#define VENT_ANGLE             (30.0f)

/* 追光比例和增量式 PID 参数。 */
#define TRACK_GAIN             (45.0f)
#define PID_KP                 (1.2f)
#define PID_KI                 (0.05f)
#define PID_KD                 (0.1f)

/* 控制周期、通信周期和各类超时，单位毫秒。 */
#define SUB_WINDOW_TRAVEL_MS   (5000u)
#define STALL_CURRENT_MA       (800)
#define MANUAL_TIMEOUT_MS      (30000u)
#define SAMPLE_PERIOD_MS       (100u)
#define STATUS_UPLOAD_MS       (500u)
#define SAFE_LOCK_RESET_MS     (60000u)
#define COMM_TIMEOUT_MS        (1000u)
#define STATE_CONFIRM_MS       (300u)

/* 传感器与滤波容量。 */
#define FILTER_MOVING_WINDOW   (5u)
#define FILTER_MEDIAN_WINDOW   (5u)
#define FILTER_MAX_WINDOW      (9u)
#define SENSOR_CHANNEL_COUNT   (8u)

/* ADC 桩返回毫伏值；原始满量程用于断线与饱和检查。 */
#define ADC_REF_MV             (3300)
#define ADC_SATURATION_MV      (3299)
#define ADC_DISCONNECT_MV      (1)

/* 传感器物理量程。越界数据被标记为无效，不参与安全决策。 */
#define TEMP_SENSOR_MIN_C      (-40.0f)
#define TEMP_SENSOR_MAX_C      (125.0f)
#define HUMIDITY_SENSOR_MIN    (0.0f)
#define HUMIDITY_SENSOR_MAX    (100.0f)
#define WIND_SENSOR_MIN_MPS    (0.0f)
#define WIND_SENSOR_MAX_MPS    (40.0f)
#define RAIN_SENSOR_MIN        (0.0f)
#define RAIN_SENSOR_MAX        (1.0f)
#define LIGHT_SENSOR_MIN       (0.0f)
#define LIGHT_SENSOR_MAX       (1000.0f)

/*
 * 传感器通道映射。默认值与 hal_stub.c 的模拟通道一致；
 * 移植时仅需按实际板卡修改这一组宏。
 */
#define SENSOR_ADC_TEMPERATURE (0u)
#define SENSOR_ADC_HUMIDITY    (1u)
#define SENSOR_ADC_WIND        (2u)
#define SENSOR_ADC_RAIN        (3u)
#define SENSOR_ADC_LIGHT_LEFT  (4u)
#define SENSOR_ADC_LIGHT_RIGHT (5u)
#define SENSOR_ADC_LIGHT_UP    (6u)
#define SENSOR_ADC_LIGHT_DOWN  (7u)

/* 默认模拟量变换：mV -> 工程单位。 */
#define TEMP_MV_TO_C           (0.1f)
#define HUMIDITY_MV_TO_PCT     (0.1f)
#define WIND_MV_TO_MPS         (0.01f)
#define RAIN_MV_TO_LEVEL       (0.001f)
#define LIGHT_MV_TO_LEVEL      (0.1f)

/* 电机驱动通道和方向、限位 GPIO 映射。 */
#define HAL_PWM_MAIN_CHANNEL   (0u)
#define HAL_PWM_SUB_CHANNEL    (1u)
#define MOTOR_PIN_MAIN_IN1     (0u)
#define MOTOR_PIN_MAIN_IN2     (1u)
#define MOTOR_PIN_SUB_IN1      (2u)
#define MOTOR_PIN_SUB_IN2      (3u)
#define LIMIT_PIN_MAIN_MIN     (4u)
#define LIMIT_PIN_MAIN_MAX     (5u)
#define LIMIT_PIN_SUB_CLOSED   (6u)
#define LIMIT_PIN_SUB_OPEN     (7u)

/* 电流采样与编码器换算。 */
#define ADC_CHANNEL_MOTOR_CURRENT (8u)
#define CURRENT_MV_PER_AMP        (100.0f)
#define CURRENT_MA_PER_AMP        (1000.0f)
#define MOTOR_CURRENT_MAX_MA      (100000)
#define CURRENT_SAMPLE_MA_OFFSET  (0)
#define ENCODER_COUNTS_PER_DEG    (10.0f)
#define ENCODER_ZERO_COUNTS       (0)

/* 执行器PWM边界与副窗固定速度。 */
#define PWM_MIN_DUTY              (0.0f)
#define PWM_MAX_DUTY              (1.0f)
#define SUB_MOTOR_DUTY            (0.65f)

/* 光照垂直差只在超过其独立阈值时辅助追光状态判断。 */
#define LIGHT_VERTICAL_FACTOR     (0.75f)

/* 双滤波融合权重：平均值权重与中值权重之和为 1。 */
#define FILTER_AVERAGE_WEIGHT     (0.5f)
#define FILTER_MEDIAN_WEIGHT      (0.5f)

/* 共享结构体读取重试次数和手动帧标志。 */
#define SNAPSHOT_READ_RETRIES     (3u)
#define MANUAL_CRC_POLYNOMIAL     (0x07u)
#define MANUAL_FLAG_SUB_OPEN      (0x01u)
#define MANUAL_FLAG_RESET         (0x02u)

/* HAL模拟资源容量，所有访问都在此范围内检查。 */
#define HAL_MOCK_ADC_CHANNELS     (12u)
#define HAL_MOCK_PWM_CHANNELS     (4u)
#define HAL_MOCK_GPIO_COUNT       (16u)

/* 通信解析帧格式：0xA5, CRC8, 角度(float), 副窗标志, 复位标志。 */
#define MANUAL_FRAME_HEADER       (0xA5u)
#define MANUAL_FRAME_LENGTH       (8u)
#define MANUAL_FRAME_CRC_INDEX    (1u)

/* 状态上传固定容量，不进行动态内存分配。 */
#define STATUS_TEXT_CAPACITY      (160u)
#define STATUS_HISTORY_DEPTH      (4u)

/* 可移植性标记：宿主测试可改用外部符号，不改变核心接口。 */
#ifndef SW_STATIC_ASSERT
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define SW_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
#define SW_STATIC_ASSERT(cond, msg) typedef char sw_static_assert_##msg[(cond) ? 1 : -1]
#endif
#endif

/* 编译期映射检查，避免板级通道数量设置不匹配。 */
SW_STATIC_ASSERT(SENSOR_CHANNEL_COUNT <= HAL_MOCK_ADC_CHANNELS,
                 sensor_channels_fit);
SW_STATIC_ASSERT(ADC_CHANNEL_MOTOR_CURRENT < HAL_MOCK_ADC_CHANNELS,
                 motor_current_channel_fit);
SW_STATIC_ASSERT(HAL_PWM_SUB_CHANNEL < HAL_MOCK_PWM_CHANNELS,
                 pwm_channels_fit);
SW_STATIC_ASSERT(LIMIT_PIN_SUB_OPEN < HAL_MOCK_GPIO_COUNT,
                 gpio_channels_fit);
SW_STATIC_ASSERT(FILTER_MOVING_WINDOW <= FILTER_MAX_WINDOW,
                 moving_window_fit);
SW_STATIC_ASSERT(FILTER_MEDIAN_WINDOW <= FILTER_MAX_WINDOW,
                 median_window_fit);

#endif /* SMART_WINDOW_CONFIG_H */
