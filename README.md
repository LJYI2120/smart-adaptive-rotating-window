# 项目名称：基于辐射制冷膜的可旋转智能窗建筑节能系统
> ⚠️ 重要版权提示
本仓库为**仅展示用途**。任何人如需使用、引用、复用代码/方案，**必须获得作者书面授权**。
仅允许在线浏览学习；禁止直接复制、移植、参赛、商用。
> # 智能环境自适应旋转窗控制系统 V1.0

[![build-and-test](https://github.com/LJYI2120/smart-adaptive-rotating-window/actions/workflows/ci.yml/badge.svg)](https://github.com/LJYI2120/smart-adaptive-rotating-window/actions/workflows/ci.yml)

面向户外旋转窗的 C99 裸机控制内核。系统采集温度、湿度、风速、雨量和四向光照，经过滤波与有效性检查后，按照“安全优先、雨天保护、手动优先、通风舒适、自动追光、节能保持”的顺序作出决策，并通过主窗角度闭环和副窗行程控制执行动作。

> 当前版本提供平台无关控制内核和可测试的主机 HAL 桩。真实 STM32 电气映射、传感器标定、驱动死区和机械参数必须按目标硬件完成验证后投入使用。

## 主要特性

- 标准 C99，无 RTOS、无动态内存、无递归、无第三方运行库。
- 温度、湿度、风速、雨量各一路以及四向光照采样。
- 滑动平均与中值滤波融合，NaN、断线、饱和和量程检查。
- 六级优先级仲裁和带迟滞、稳定确认的六状态控制器。
- 主窗增量式 PID、角度死区、双端限位和 PWM 限幅。
- 副窗非阻塞开关、行程超时、目标限位和电流堵转保护。
- 风锁、雨锁、传感器故障、通信超时和电机堵转安全覆盖。
- 电机反向前后撤 PWM 并执行可配置软件死区。
- 中断与主循环共享数据使用短临界区复制。
- 固定容量状态历史与主机可执行回归测试。

## 控制架构

```text
100 ms timer
  -> env_sensor_read
  -> sensor_valid_check
  -> moving_average_filter + median_filter
  -> environment snapshot
  -> main_window_angle_ctrl -> motor -> PWM

main loop
  -> safety_override
  -> state_machine_run
       -> decision_arbitrate
  -> command snapshot -> timer
  -> sub_window_open / close
  -> sub_window_service
  -> status_upload
```

状态优先级：

```text
SAFE_LOCK > CLOSE_RAIN > MANUAL > VENTILATE > AUTO_TRACK > IDLE
```

## 目录

```text
include/
  app.h             应用调度与诊断公共接口
  config.h          全部阈值、周期、通道和超时
  types.h           固定数据结构与状态枚举
  hal.h             平台无关硬件接口
  sensor.h          环境采集
  filter.h          滤波
  decision.h        优先级仲裁
  state_machine.h   状态迁移
  main_window.h     主窗闭环
  sub_window.h      副窗行程
  safety.h          安全覆盖
  motor.h           电机驱动
src/
  hal_stub.c        主机模拟 HAL
  sensor.c          采样与有效性检查
  filter.c          移动平均与中值滤波
  decision.c        优先级决策
  state_machine.c   状态机与安全锁存
  main_window.c     增量 PID 与限位
  sub_window.c      副窗限位、超时和堵转
  safety.c          安全覆盖
  motor.c           方向、PWM、电流和死区
  app.c             采样、调度与状态上传
  main.c            裸机入口和主机演示
test/
  test_host.c       主机自动化测试
Makefile
DESIGN_NOTES.md
soft_register_order.md
```

## 构建与测试

依赖：GCC、GNU Make。

```bash
make clean
make host
make test
make run
```

生成文件：

```text
build/app_host
build/test_host
```

Windows 下 Makefile 会自动使用 `build/app_host.exe` 和
`build/test_host.exe`。

当前主机验证结果：

```text
test_host: 68 checks, 0 failures
```

测试覆盖追光、风锁、雨滴关窗、手动超时、限位停机、副窗电流堵转、行程超时、未知限位安全归位、安全锁复位、滤波器、CRC 手动帧和完整应用链路。

## 安全策略

- 堵转默认只能人工复位。可在 `config.h` 中显式启用 `MOTOR_STALL_AUTO_CLEAR_ENABLE`，但量产系统仍应结合编码器、限位和机械检查确认危险已解除。
- 副窗两个限位都未触发时进入 `SUB_WINDOW_UNKNOWN`，不会假定窗口已经关闭；打开请求会先转换为关闭归位。
- 主窗目标或反馈越过机械范围、出现 NaN/Inf 时立即停机。
- 安全状态输出副窗关闭、主窗安全角，并锁存至复位条件满足。

## 参数配置

环境阈值、PID 参数、角度、超时、电机通道和传感器映射均位于 [`include/config.h`](include/config.h)。修改机械或电气参数后，应重新执行全部主机测试并完成硬件在环验证。

## STM32 移植清单

1. 实现 `hal_adc_read`、`hal_pwm_set`、`hal_encoder_read`、GPIO、毫秒时钟和电机停机接口。
2. 实现 `hal_delay_us`，或确认驱动芯片已提供等效硬件死区。
3. 实现可嵌套的 `hal_critical_enter` / `hal_critical_exit`，保存并恢复原中断状态。
4. 用硬件定时器按 `SAMPLE_PERIOD_MS` 调用 `app_timer_tick`，主循环调用 `app_run`。
5. 将通信接收中断解析出的手动命令通过 `app_set_manual_command` 安装。
6. 完成 ADC 校准、传感器量程、编码器方向和限位有效电平标定。
7. 在带真实负载的试验台上验证堵转、超时、断电恢复和限位失效。

## 已知限制

- 不包含具体 MCU 厂商工程、链接脚本和启动文件。
- 不包含实体传感器、电机驱动板和机械结构参数。
- `status_upload` 生成固定容量快照，实际 UART/CAN 发送需在移植层接入。
- 主机测试不能替代安全、电气、EMC、耐久和机械认证。

## 文档

- [`DESIGN_NOTES.md`](DESIGN_NOTES.md)：数据流、状态迁移、主副窗联动、阈值与异常表。
- [`soft_register_order.md`](soft_register_order.md)：软著源代码前后 30 页裁切建议。

## 使用与许可

本仓库当前未声明开源许可证。公开可见不等于授权复制、修改、分发或商业使用；如需开放复用，请由权利人明确选择并添加许可证。
版权所有 ©2026【刘俊逸】，保留一切权利。
本项目包含源代码、文档、设计材料均为作者知识产权。未经作者书面授权，不得用于任何复制、修改、二次分发、竞赛、商业项目等使用行为。
授权申请请联系：【748942850@qq.com】
