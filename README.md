# 基于辐射制冷膜的可旋转智能窗建筑节能系统

> **重要版权提示**
>
> 本仓库仅用于展示。使用、引用或复用代码和方案必须获得作者书面授权。
> 仅允许在线浏览学习；禁止直接复制、移植、参赛或商用。

## 软件信息

| 项目 | 内容 |
|---|---|
| 软件名称 | 智能环境自适应旋转窗控制系统 |
| 软著版本 | V1.0 |
| PC 原型版本 | 0.1.0 |
| 实现语言 | Python 3、C++17、C99 |
| 运行平台 | Windows PC + ESP32 |

[![build-and-test](https://github.com/LJYI2120/smart-adaptive-rotating-window/actions/workflows/ci.yml/badge.svg)](https://github.com/LJYI2120/smart-adaptive-rotating-window/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/LJYI2120/smart-adaptive-rotating-window)](https://github.com/LJYI2120/smart-adaptive-rotating-window/releases)

面向辐射制冷智能窗的“PC 高级决策 + ESP32 本地安全执行”原型。PC 软件负责太阳方位计算、追光目标、模式切换、实时曲线与日志；ESP32 负责四路光照采集、串口协议、步进电机脉冲、限位、急停和通信看门狗。

仓库同时保留原有的 C99 裸机控制内核，用于环境安全仲裁、PWM 电机控制和主机仿真参考。实际竞赛原型使用新增的 `pc_app` 和 `firmware/esp32`。

![PC control application](docs/images/pc_app.png)

## 版本更新

### v1.0.1

- 堵转故障默认要求人工复位，自动恢复改为显式配置。
- 副窗位置未知时先执行关闭归位，不再假定窗口已经关闭。
- 共享数据改用可嵌套临界区复制，并补充电机换向死区。
- 增加 `app.h` 公共接口、README、自动构建和 UBSan 检查。
- 统一应用接口注释和设计文档表述。

## 主要特性

- Windows 桌面应用支持串口扫描、自动连接、一键启动和停止。
- PC 端计算太阳方位、归一化光强 PID 修正、模式目标和运动限速。
- ESP32 直接读取 TCA9548A 后的四路 TSL2561，并回传实时光强。
- 系统固件使用 CRC16 帧、设备握手、命令确认和通信看门狗。
- 非阻塞 STEP/DIR 脉冲、启动归零、NC 限位和物理急停保护。
- PyInstaller 可生成脱离 Python 环境的 Windows 发布目录。
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

## 系统架构

```text
Windows PC application
  ├─ solar position and tracking target
  ├─ mode and parameter selection
  ├─ relative-light chart and logs
  └─ CRC-protected commands, 250 ms heartbeat
                    │ USB serial 115200 8N1
                    ▼
ESP32 firmware
  ├─ TCA9548A + four TSL2561 sensors
  ├─ 1500 ms command watchdog
  ├─ NC limit and physical emergency stop
  ├─ homing and non-blocking STEP/DIR
  └─ 200 ms telemetry
                    │
                    ▼
TMC2209 -> NEMA17 -> single-axis window
```

## C99 control core

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
pc_app/
  src/smart_window/ Windows 桌面应用
  tests/            Python 自动化测试
firmware/esp32/     PlatformIO 固件
protocol/           串口协议说明
docs/               接线、操作和竞赛材料
Makefile
DESIGN_NOTES.md
soft_register_order.md
```

## 构建与测试

### Windows PC 应用

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\pc_app\build_windows.ps1
```

生成目录：`pc_app/dist/SmartWindowController/`。

```text
PC application tests: 18 passed
```

### ESP32 固件

使用 PlatformIO 打开 `firmware/esp32` 并选择 `esp32dev` 环境；命令行构建：

```bash
pio run -d firmware/esp32
pio test -d firmware/esp32 -e native_protocol
```

### C99 控制内核

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

- 默认引脚、电机比例和传感器方向需要与实际接线核对。
- TSL2561 数据为相对可见光计数，尚未完成照度计标定。
- 当前采用限位归零加步进计数；可选 AS5600 需要完成机械零位标定。
- CH340 驱动、24 V 电源和急停接线必须按实物验证。
- C99 参考内核中的 UART/CAN 上传仍需实际平台接入。
- 主机测试不能替代安全、电气、EMC、耐久和机械认证。

## 文档

- [`DESIGN_NOTES.md`](DESIGN_NOTES.md)：数据流、状态迁移、主副窗联动、阈值与异常表。
- [`soft_register_order.md`](soft_register_order.md)：软著源代码前后 30 页裁切建议。
- [`protocol/PROTOCOL.md`](protocol/PROTOCOL.md)：PC 与 ESP32 串口协议。
- [`docs/WIRING.md`](docs/WIRING.md)：接线、电源与安全输入。
- [`docs/OPERATIONS.md`](docs/OPERATIONS.md)：PC 软件操作和故障处理。
- [`docs/CONTEST_PLAN.md`](docs/CONTEST_PLAN.md)：竞赛选型、预算和测试计划。
- [`docs/SOFTWARE_COPYRIGHT.md`](docs/SOFTWARE_COPYRIGHT.md)：PC 与固件软著材料映射。
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)：桌面端依赖和许可证说明。
- [GitHub Releases](https://github.com/LJYI2120/smart-adaptive-rotating-window/releases)：固定版本源码快照。

## 版权与授权

本仓库当前未声明开源许可证。公开可见不等于授权复制、修改、分发或商业使用；如需开放复用，请由权利人明确选择并添加许可证。

版权所有 © 2026 刘俊逸，保留一切权利。源代码、文档和设计材料均为作者知识产权。未经作者书面授权，不得复制、修改、二次分发、用于竞赛或商业项目。

授权申请联系：`748942850@qq.com`
