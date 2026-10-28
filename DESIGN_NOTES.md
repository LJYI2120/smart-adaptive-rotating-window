# 智能环境自适应旋转窗控制系统 V1.0 设计与说明书钩子

## 1. 设计目标

系统以裸机 C99 实现户外环境采集、舒适与安全仲裁、主窗旋转闭环和副窗开关控制。核心约束为：

- 不使用 RTOS、动态内存、递归或第三方运行库。
- 所有硬件访问经过 `hal_xxx` 接口。
- 环境阈值、控制参数、超时和通道映射集中在 `include/config.h`。
- 主循环负责模式裁决和副窗决策；周期定时器回调负责采样、滤波、主窗闭环和 PWM 更新。
- 安全覆盖优先于任何舒适、手动或追光命令。

## 2. 总体数据流图（文字版）

```text
┌──────────────────────────────────────────────────────────────┐
│ 100 ms 周期定时器回调 app_timer_tick                          │
│                                                              │
│ ADC0..ADC7                                                   │
│   │                                                          │
│   ▼                                                          │
│ env_sensor_read                                              │
│   ├─ 温度、湿度、风速、雨量                                  │
│   └─ 左、右、上、下光照                                      │
│   │                                                          │
│   ▼                                                          │
│ sensor_valid_check -> valid_mask                             │
│   │                                                          │
│   ▼                                                          │
│ moving_average_filter + median_filter                        │
│   │                                                          │
│   ▼                                                          │
│ 环境快照序号锁 -> g_env_filtered                              │
│   │                                                          │
│   └─ 读取命令序号锁 -> 主窗目标角                             │
│          │                                                   │
│          ▼                                                   │
│      main_window_angle_ctrl                                  │
│          ├─ 角度死区 -> 停机                                  │
│          ├─ 增量 PID -> PWM 0.0..1.0                          │
│          └─ 编码器反馈 -> 方向与PWM                           │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ 裸机主循环 app_run                                            │
│                                                              │
│ 环境快照                                                     │
│   │                                                          │
│   ▼                                                          │
│ safety_override                                              │
│   ├─ 风速锁                                                   │
│   ├─ 雨量锁                                                   │
│   ├─ 传感器故障                                               │
│   ├─ 通信超时                                                 │
│   └─ 电机堵转粘滞                                             │
│   │                                                          │
│   ▼                                                          │
│ state_machine_run                                            │
│   └─ decision_arbitrate                                      │
│       安全 > 雨天 > 手动 > 通风 > 追光 > 保持                 │
│   │                                                          │
│   ▼                                                          │
│ window_cmd_t                                                 │
│   ├─ 主窗目标角 -> 命令序号锁 -> 定时器                       │
│   └─ 副窗开关 -> sub_window_open/close                       │
│                                                              │
│ sub_window_service                                           │
│   ├─ 限位到位                                                 │
│   ├─ 电流堵转                                                 │
│   └─ 行程超时                                                 │
│   │                                                          │
│   ▼                                                          │
│ status_upload -> 固定历史快照                                 │
└──────────────────────────────────────────────────────────────┘
```

关键时间关系：

| 任务 | 入口 | 周期/触发 | 阻塞要求 |
|---|---|---|---|
| 环境采样与主窗闭环 | `app_timer_tick` | 100 ms 定时器 | 不做长延时 |
| 安全与模式裁决 | `app_run` | 主循环持续调用 | 非阻塞 |
| 副窗行程推进 | `sub_window_service` | 主循环每轮 | 非阻塞 |
| 状态上传 | `status_upload` | 500 ms 门限 | 只生成快照 |
| 手动超时 | `decision_manual_is_valid` | 每轮仲裁 | 按时间戳计算 |
| 行程超时 | `sub_window_service` | 每轮服务 | 按启动时刻计算 |

## 3. 状态迁移图（文字版）

```text
                    任一安全标志置位
        ┌────────────────────────────────────┐
        │                                    ▼
   正常自动/手动/雨天 ────────────────> ST_SAFE_LOCK
                                             │
                                             │ 安全条件消失
                                             │ + manual_reset
                                             │ 或 SAFE_LOCK_RESET_MS
                                             ▼
                                         ST_IDLE
                                             │
                    ┌────────────────────────┼──────────────────────┐
                    ▼                        ▼                      ▼
              ST_AUTO_TRACK           ST_VENTILATE            ST_CLOSE_RAIN
                    │                        │                      │
                    │ 光差进死区              │ 温湿度回迟滞内侧      │ 雨量低于
                    └──────────> ST_IDLE <────┘                      │ DETECT-HYST
                                                                     ▼
                                                                  ST_IDLE

    有效手动帧且未超时：
      任意非安全状态 ─────────────────────> ST_MANUAL
      ST_MANUAL + 30 s 超时 ──────────────> 自动规则重新仲裁
```

状态定义与迁移条件：

| 状态 | 代码枚举 | 进入条件 | 主窗目标 | 副窗 | 离开条件 |
|---|---|---|---:|---|---|
| 安全锁 | `ST_SAFE_LOCK` | 风锁、雨锁、传感器故障、通信超时、堵转 | `SAFE_ANGLE` | 关 | 故障清除且人工复位或 60 s 超时 |
| 雨天保护 | `ST_CLOSE_RAIN` | 雨量 ≥ `RAIN_DETECT` | `RAIN_ANGLE` | 关 | 雨量低于检测值减迟滞 |
| 手动 | `ST_MANUAL` | 有效帧、角度合法、年龄 ≤ 30 s | 手动角度 | 手动开关 | 30 s 超时或更高优先级 |
| 通风 | `ST_VENTILATE` | 温度/湿度越过舒适区、风速安全、无雨 | `VENT_ANGLE` | 开 | 回到舒适区迟滞内侧 |
| 自动追光 | `ST_AUTO_TRACK` | 水平或垂直光差超过死区、无通风需求 | 水平光差 × 45° | 关 | 光差进入死区 |
| 节能保持 | `ST_IDLE` | 无更高优先级且光差在死区 | 保持原角度 | 关 | 出现更高优先级或自动需求 |

自动通风、追光和保持之间使用 `STATE_CONFIRM_MS = 300 ms` 连续候选确认。安全、雨滴和手动迁移即时生效。

## 4. 主副窗联动图（文字版）

```text
                    ┌──────────────────────┐
                    │ decision_arbitrate   │
                    └──────────┬───────────┘
                               │
             ┌─────────────────┼──────────────────┐
             ▼                 ▼                  ▼
       SAFE/RAIN/TRACK      MANUAL             VENTILATE
       main = 0/0/track     main = manual       main = 30°
       sub = CLOSE          sub = manual        sub = OPEN
             │                 │                  │
             └─────────────────┴──────────────────┘
                               │
                               ▼
                  sub_window_open / close
                               │
                     ┌─────────┴─────────┐
                     ▼                   ▼
                 已到位则停          未到位则驱动
                                         │
                          ┌──────────────┼───────────────┐
                          ▼              ▼               ▼
                      目标限位      电流 > 800 mA     5 s 未到位
                          │              │               │
                          ▼              ▼               ▼
                       正常停止       motor_stall=1   motor_stall=1
                                             │
                                             ▼
                                     下一轮 ST_SAFE_LOCK
```

联动规则：

1. 安全、雨天、追光和节能状态都要求副窗关闭。
2. 通风状态要求副窗打开，主窗到 30°。
3. 手动状态同时服从手动主窗角度和手动副窗开关。
4. 副窗运动采用启动命令加周期服务，`sub_window_open` 和 `sub_window_close` 不忙等。
5. 任一主/副窗堵转都会通过 `safety_status_t.motor_stall` 进入安全锁，主窗回安全角，副窗关闭。

## 5. 函数名与说明书章节对应

| 说明书章节 | 函数/符号 | 文件 |
|---|---|---|
| 3.2 系统数据模型 | `system_state_t`、`env_data_t`、`window_cmd_t`、`safety_status_t`、`manual_cmd_t` | `include/types.h` |
| 3.3 软件运行架构 | `app_init`、`app_run`、`app_timer_tick` | `src/app.c`、`src/main.c` |
| 3.4 硬件抽象层 | `hal_adc_read`、`hal_pwm_set`、`hal_encoder_read`、`hal_gpio_read`、`hal_gpio_write` | `include/hal.h`、`src/hal_stub.c` |
| 4.1 环境数据采集 | `env_sensor_read`、`sensor_valid_check` | `src/sensor.c` |
| 4.2 采集数据滤波 | `moving_average_filter`、`median_filter` | `src/filter.c` |
| 4.3 决策仲裁逻辑 | `decision_arbitrate`、`decision_manual_is_valid`、`decision_track_angle` | `src/decision.c` |
| 4.4 工作状态机 | `state_machine_run`、`state_machine_reset` | `src/state_machine.c` |
| 4.5 主窗闭环控制 | `main_window_angle_ctrl`、`limit_check` | `src/main_window.c` |
| 4.5.3 编码器反馈 | `motor_read_main_angle_deg` | `src/motor.c` |
| 4.5.6 定时器控制回调 | `app_timer_tick` | `src/app.c` |
| 4.6 副窗执行控制 | `sub_window_open`、`sub_window_close`、`sub_window_service` | `src/sub_window.c` |
| 4.6.7 电流堵转保护 | `motor_stall_protect`、`motor_mark_stall` | `src/motor.c` |
| 4.7 安全覆盖与异常处理 | `safety_override`、`safety_clear_latches` | `src/safety.c` |
| 4.8.2 手动命令解析 | `manual_cmd_parse` | `src/app.c` |
| 4.8.4 状态上传 | `status_upload` | `src/app.c` |
| 10.1 主机测试环境 | `hal_reset_mock` 及 `hal_set_mock_*` | `src/hal_stub.c` |
| 10.2 主机自动化测试 | `main` 中的场景函数 | `test/test_host.c` |

## 6. 阈值表

所有可调环境阈值和运行参数均位于 `include/config.h`。

| 参数 | 默认值 | 单位 | 用途 |
|---|---:|---|---|
| `TEMP_COMFORT_MIN` | 16.0 | °C | 温度舒适下界 |
| `TEMP_COMFORT_MAX` | 28.0 | °C | 温度舒适上界 |
| `TEMP_HYST` | 1.0 | °C | 温度退出迟滞 |
| `HUMIDITY_COMFORT_MIN` | 40.0 | % | 湿度舒适下界 |
| `HUMIDITY_COMFORT_MAX` | 70.0 | % | 湿度舒适上界 |
| `HUM_HYST` | 3.0 | % | 湿度退出迟滞 |
| `WIND_SAFE_MPS` | 8.0 | m/s | 通风允许上限 |
| `WIND_LOCK_MPS` | 12.0 | m/s | 风速安全锁 |
| `WIND_HYST` | 1.0 | m/s | 风锁退出迟滞 |
| `RAIN_DETECT` | 0.2 | 归一化 | 雨滴关窗门限 |
| `RAIN_LOCK` | 0.8 | 归一化 | 雨量安全锁 |
| `RAIN_HYST` | 0.05 | 归一化 | 雨天退出迟滞 |
| `LIGHT_DEADBAND` | 0.05 | 归一化 | 水平光差死区 |
| `LIGHT_VERTICAL_FACTOR` | 0.75 | 比例 | 垂直差辅助阈值系数 |
| `ANGLE_DEADBAND` | 1.5 | ° | 主窗停机死区 |
| `MAIN_ANGLE_MIN` | -90.0 | ° | 主窗机械下界 |
| `MAIN_ANGLE_MAX` | 90.0 | ° | 主窗机械上界 |
| `SAFE_ANGLE` | 0.0 | ° | 安全目标角 |
| `RAIN_ANGLE` | 0.0 | ° | 防雨目标角 |
| `VENT_ANGLE` | 30.0 | ° | 通风目标角 |
| `TRACK_GAIN` | 45.0 | °/光差 | 追光比例 |
| `PID_KP` | 1.2 | 比例 | 增量 PID 比例项 |
| `PID_KI` | 0.05 | 积分 | 增量 PID 积分项 |
| `PID_KD` | 0.1 | 微分 | 增量 PID 微分项 |
| `SUB_WINDOW_TRAVEL_MS` | 5000 | ms | 副窗行程超时 |
| `STALL_CURRENT_MA` | 800 | mA | 堵转电流门限 |
| `MANUAL_TIMEOUT_MS` | 30000 | ms | 手动命令有效期 |
| `SAMPLE_PERIOD_MS` | 100 | ms | 采样和闭环周期 |
| `STATUS_UPLOAD_MS` | 500 | ms | 状态上传周期 |
| `SAFE_LOCK_RESET_MS` | 60000 | ms | 安全自动超时复位 |
| `COMM_TIMEOUT_MS` | 1000 | ms | 环境快照通信超时 |
| `STATE_CONFIRM_MS` | 300 | ms | 自动状态迁移确认 |

## 7. 异常处理表

| 异常 | 检测位置 | 判定条件 | 立即动作 | 恢复策略 |
|---|---|---|---|---|
| 温度断线/越界/NaN | `sensor_valid_check` | 原始值 ≤ 1 mV、≥ 3299 mV 或温度超出 -40..125°C | 清温度有效位 | 原始值和量程恢复 |
| 湿度断线/饱和 | `sensor_valid_check` | 原始值异常或湿度不在 0..100% | 清湿度有效位 | 有效位恢复 |
| 风速断线/超量程 | `sensor_valid_check` | 原始值异常或风速不在 0..40 m/s | 清风速有效位 | 有效位恢复 |
| 雨量断线/超量程 | `sensor_valid_check` | 原始值异常或雨量不在 0..1 | 清雨量有效位 | 有效位恢复 |
| 严重传感器故障 | `safety_override` | 任一必需 `valid_mask` 位为 0 | `sensor_fault=1`，安全锁 | 全部必需位恢复，再按安全锁策略 |
| 强风 | `safety_override` | 风速 ≥ 12 m/s | `wind_lock=1`，安全锁 | 风速 ≤ 11 m/s 后清标志，再执行安全锁复位 |
| 强雨 | `safety_override` | 雨量 ≥ 0.8 | `rain_lock=1`，安全锁 | 雨量 ≤ 0.75 后清标志 |
| 雨滴 | `decision_arbitrate` | 雨量 ≥ 0.2 | `ST_CLOSE_RAIN`，副窗关 | 雨量低于 0.15 回自动 |
| 通信超时 | `safety_override` | 环境快照年龄 > 1000 ms | `comm_timeout=1`，安全锁 | 新时间戳进入窗口 |
| 主窗越限 | `limit_check` | 角度非有限或越过 ±90° | PWM=0，`hal_motor_stop` | 反馈回到范围 |
| 主窗角度死区 | `main_window_angle_ctrl` | 绝对误差 ≤ 1.5° | PWM=0，清 PID 历史 | 再次超出死区 |
| 副窗电流堵转 | `motor_stall_protect` | 驱动中电流 > 800 mA | 停机，`motor_stall=1` | 人工复位或 60 s 超时且电流正常 |
| 副窗行程超时 | `sub_window_service` | 5 s 未到目标限位 | 停机，`motor_stall=1` | 人工复位或超时策略 |
| 双限位同时有效 | `sub_identify_position` | 关闭和打开限位同时为高 | 副窗故障，堵转标志 | 人工检查并复位 |
| 手动帧长度错误 | `manual_cmd_parse` | 长度不是 8 字节 | 丢弃 | 下一完整帧 |
| 手动帧头或 CRC 错误 | `manual_cmd_parse` | 帧头不是 0xA5 或 CRC8 不匹配 | 丢弃 | 下一完整帧 |
| 手动角度非法 | `manual_cmd_parse`、`decision_manual_is_valid` | 非有限或超出 ±90° | 丢弃手动优先级 | 新合法命令 |
| 手动超时 | `decision_manual_is_valid` | 年龄 > 30 s | 回落自动仲裁 | 新命令或自动模式 |
| 安全锁存未恢复 | `state_machine_run` | 故障清除但无复位且未到 60 s | 继续安全角、副窗关 | 手动复位或超时 |

## 8. 并发与快照一致性

目标系统为裸机模型，但仍可能存在定时器与主循环交错。工程采用两个序号锁：

1. `g_environment_sequence` 保护定时器写入、主循环读取的环境快照。
2. `g_command_sequence` 保护主循环写入、定时器读取的窗口命令。

写入方在一次结构体更新前后分别递增序号，读取方仅在前后序号一致且为偶数时采用数据。重试三次仍冲突时，环境默认全无效，命令默认安全角，确保失败方向是保守停机。

## 9. 主机验证记录

验证日期：2026-10-28。

验证条件：

- C 语言标准：C99。
- 严格参数：`-std=c99 -Wall -Wextra -Wpedantic -Werror`。
- 主机程序：`build/app_host.exe`。
- 测试程序：`build/test_host.exe`。
- 构建工具说明：本机未预装 GCC/Make，使用无项目依赖的 Zig C 前端执行等价严格编译；工程 Makefile 固定调用 `gcc`，在具备 GCC 的主机可直接运行。

实际结果：

```text
Smart window host demo completed
state=AUTO_TRACK angle_target=90.00 wind=0.60 uploads=7
timer_ticks=39 simulated_ms=4000

test_host: 58 checks, 0 failures
```

覆盖场景：

- 正常水平追光与垂直差辅助触发。
- 风速锁定及恢复迟滞。
- 安全条件恢复后仍保持锁存，人工复位后才退出。
- 雨滴关窗而不误触发雨量安全锁。
- 手动角度和副窗命令优先。
- 30 s 手动超时回落自动追光。
- 主窗双侧限位、NaN 输入和停电机。
- 副窗电流堵转。
- 副窗 5 s 行程超时。
- 传感器 NaN 与量程越界。
- 滑动平均与中值尖峰抑制。
- 8 字节手动帧 CRC 解析与损坏帧拒绝。
- 从 HAL 仿真到采样、滤波、仲裁、主窗控制和上传的集成链路。

## 10. 自检清单

- [x] 主提示词列出的 22 个工程文件全部存在。
- [x] `system_state_t`、`env_data_t`、`window_cmd_t`、`safety_status_t`、`manual_cmd_t` 名称和字段稳定。
- [x] 关键函数签名与主提示词一致。
- [x] 每个声明函数均有完整实现。
- [x] 无 `TODO`、`FIXME`、空函数体或省略号占位。
- [x] 不使用 `malloc`、`free`、递归和第三方运行库。
- [x] 阈值、周期、PID、角度和超时集中在 `config.h`。
- [x] 所有数组访问有长度或索引保护。
- [x] NaN、Inf、除零风险和传感器饱和均有保护。
- [x] 主窗 PWM 限制在 0.0..1.0，死区停机。
- [x] 主窗双端限位越界停机。
- [x] 副窗包含行程超时、目标限位和堵转保护。
- [x] 风、雨、传感器、通信和堵转均能触发安全覆盖。
- [x] 主循环裁决与定时器采样/闭环职责分离。
- [x] 主机版严格告警编译成功。
- [x] 7 个必测场景全部覆盖，另有 6 个补充场景通过，共 58 项断言。
- [x] 环境与命令共享结构体使用序号锁。
- [x] `soft_register_order.md` 提供前 30 页和后 30 页顺序。
- [x] `DESIGN_NOTES.md` 提供三张图、函数映射、阈值表和异常表。
