# 智能环境自适应旋转窗控制系统 V1.0 源代码裁切顺序

## 1. 裁切原则

软著源程序材料建议采用 A4 纵向、每页 50 行、页眉包含软件全称和版本号的统一格式。前 30 页展示系统的数据入口、滤波、优先级仲裁和状态迁移；后 30 页展示主窗闭环、副窗行程保护、安全覆盖和异常恢复。

裁切必须遵守以下约束：

1. 页面边界落在完整函数结束处，禁止把函数声明、复合语句或注释块切成两页。
2. 页眉统一为“智能环境自适应旋转窗控制系统 V1.0 源代码 第 X 页”。
3. 文件之间保留文件页眉，清晰显示文件名称、模块说明和对应说明书章节。
4. 前后两部分采用同一提交版本和同一编码（UTF-8）生成，避免行号与正文不一致。
5. 若每页不是 50 行，应以整函数边界为准微调，不以删除必要空行或拼接半截函数凑页数。
6. 行号只作为排版辅助，不写入 C 源码，防止复制编译时出现非 C 内容。

当前工程 C 源码和主机测试约 3,700 行，具备从完整函数边界选取连续 30 页的余量。

## 2. 前 30 页建议顺序

前 30 页突出“环境数据如何进入系统、如何被清洗、如何被仲裁为窗口动作”。建议从 `src/app.c` 的初始化和定时器入口开始，再回看数据类型、HAL、采集、滤波、决策与状态机。

| 顺序 | 文件 | 建议函数/区间 | 申报展示重点 | 目标页数 |
|---:|---|---|---|---:|
| 1 | `include/types.h` | 固定状态枚举、环境数据结构、安全结构 | 说明系统状态和数据边界 | 2 |
| 2 | `include/config.h` | 阈值、采样周期、PID、角度范围 | 集中参数管理 | 3 |
| 3 | `include/hal.h` + `src/hal_stub.c` | HAL 接口、模拟 ADC/PWM/编码器 | 平台隔离与可测试性 | 3 |
| 4 | `src/sensor.c` | `sensor_init` 至 `sensor_valid_check` | 七通道采集、NaN/量程/断线检查 | 5 |
| 5 | `src/filter.c` | `moving_average_filter`、`median_filter` | 噪声抑制与尖峰剔除 | 3 |
| 6 | `src/app.c` | `app_crc8`、`manual_cmd_parse`、`app_filter_environment` | 通信校验和滤波融合入口 | 4 |
| 7 | `src/decision.c` | `decision_manual_is_valid` 至 `decision_arbitrate` | 六级优先级仲裁 | 7 |
| 8 | `src/state_machine.c` | `state_machine_reset` 至 `state_machine_run` | 迟滞、稳定确认、安全锁存 | 3 |

上述区间合计约 30 页。若页面行高变化，优先保留 `decision_arbitrate` 和 `state_machine_run` 的完整函数，不拆分核心判断分支。

前 30 页的阅读主线：

```text
ADC/HAL
  -> env_sensor_read
  -> sensor_valid_check
  -> moving_average_filter + median_filter
  -> safety_override
  -> decision_arbitrate
  -> state_machine_run
  -> window_cmd_t
```

## 3. 后 30 页建议顺序

后 30 页突出“命令如何安全地变成电机动作，以及异常如何阻断动作”。应从主窗闭环开始，依次展示副窗行程、电流采集、安全联动、主循环与测试证据。

| 顺序 | 文件 | 建议函数/区间 | 申报展示重点 | 目标页数 |
|---:|---|---|---|---:|
| 1 | `include/main_window.h` | PID 接口和限位接口 | 闭环控制边界 | 1 |
| 2 | `src/main_window.c` | `main_window_init` 至 `main_window_angle_ctrl` | 增量 PID、死区、限位停机 | 5 |
| 3 | `include/motor.h` + `src/motor.c` | 方向、PWM、电流、堵转保护 | 执行器封装和故障锁存 | 6 |
| 4 | `src/sub_window.c` | `sub_window_init` 至 `sub_window_service` | 非阻塞开关、限时、电流堵转 | 5 |
| 5 | `src/safety.c` | `safety_override` 及风、雨、通信检查 | 安全覆盖与恢复 | 6 |
| 6 | `src/app.c` | `app_run`、复位、`status_upload` | 主循环、临界区快照、状态上传 | 5 |
| 7 | `test/test_host.c` | 追光、锁定、雨滴、手动、限位、堵转测试 | 可执行验证证据 | 2 |

后 30 页的阅读主线：

```text
window_cmd_t
  -> main_window_angle_ctrl
  -> limit_check
  -> motor_set_main_output
  -> sub_window_open/close
  -> sub_window_service
  -> motor_stall_protect
  -> safety_status_t
  -> SAFE_LOCK
```

## 4. 完整函数边界检查

生成 PDF 前逐页检查以下断点：

- `env_sensor_read` 的最后右花括号之后才允许换页。
- `decision_arbitrate` 六个优先级分支不得跨页拆开。
- `state_machine_run` 的安全锁存和自动确认逻辑不得跨页拆开。
- `main_window_angle_ctrl` 的 PID 输出与方向写入必须同页展示。
- `sub_window_service` 的“电流、限位、超时”判断顺序必须完整保留。
- `safety_override` 的四类基础安全检查必须完整保留。
- 任意页首不得只出现孤立右花括号。
- 任意页尾不得停在表达式中间、字符串常量中间或块注释中间。

## 5. 页眉与首尾页示例

推荐页眉：

```text
智能环境自适应旋转窗控制系统 V1.0 源代码                         第 1 页
```

推荐首页从公共数据模型开始，并在文件页眉中体现：

```c
/* 软件名称：智能环境自适应旋转窗控制系统 V1.0
 * 文件名称：types.h
 * 模块说明：定义系统公共数据模型、状态枚举与错误码
 * 对应说明书章节：3.2 系统数据模型
 */
```

推荐末页结束于 `test_host` 的失败计数与返回值，保证最后一个函数完整：

```c
printf("test_host: %u checks, %u failures\n", g_checks, g_failures);
return (g_failures == 0u) ? 0 : 1;
}
```

## 6. 软著说明与代码的对应

提交材料中建议按下列名称描述模块，避免说明书与源码术语漂移：

| 说明书对象 | 代码对象 |
|---|---|
| 环境采样任务 | `env_sensor_read` |
| 数据有效性判据 | `sensor_valid_check`、`valid_mask` |
| 双滤波入口 | `moving_average_filter`、`median_filter` |
| 优先级仲裁器 | `decision_arbitrate` |
| 六状态控制器 | `state_machine_run`、`system_state_t` |
| 主窗角度闭环 | `main_window_angle_ctrl` |
| 副窗行程服务 | `sub_window_service` |
| 电流堵转保护 | `motor_stall_protect` |
| 安全覆盖入口 | `safety_override` |
| 手动协议解析 | `manual_cmd_parse` |
| 运行期数据上传 | `status_upload` |

## 7. 提取与质检流程

1. 在固定提交版本上执行一次 `make clean`、`make host`、`make test`。
2. 记录编译器和构建时间，保留零告警输出。
3. 按本文件顺序提取连续源码，不手工改写申报副本。
4. 使用统一模板生成页眉、页码和固定行高。
5. 抽查每页首尾，确认不存在半截函数。
6. 对比说明书中的函数名、枚举名和阈值，确保逐字一致。
7. 检查前 30 页与后 30 页的软件名称、版本号、页码格式一致。
8. 对 PDF 做文本搜索，确认没有 `TODO`、`FIXME` 或占位函数。

## 8. 最终检查点

- [x] 前 30 页覆盖初始化、采集、滤波、决策和状态机。
- [x] 后 30 页覆盖主窗、副窗、限位、堵转、安全和异常处理。
- [x] 页边界要求按完整函数裁切。
- [x] 已给出统一页眉格式。
- [x] 每个源文件都有软件名称、文件名称、模块说明和说明书章节页眉。
- [x] 代码对象与说明书术语建立了逐项对应表。
