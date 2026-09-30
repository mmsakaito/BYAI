# BYAI PRD：首版硬件适配样机

本文定义首版需求、范围和验收目标。实施与验收证据记录在 Issue，动态进度在 [BYAI · 开发路线 Project](https://github.com/users/mmsakaito/projects/1) 维护。

> 当前仓库尚未提交固件、构建系统或自动测试。本文中的能力与指标均为待实现、待验证的目标，不代表已完成或已通过验收。

## 1. 产品目标

BYAI 面向 ESP32-S3 AI 语音交互终端方向。首版服务于团队自用验证：在现有硬件上建立可复现构建、稳定运行、主要硬件路径可验证的样机，为下一版完整 AI 对话体验打基础。

首版固定使用现有芯片和主板，不主动更换硬件。若实现需求需要新增硬件或改板，先记录缺口，再重新讨论范围。

## 2. 使用场景与首版范围

团队成员按文档准备环境、构建并烧录固件，启动后通过基础自检界面查看网络和外设状态，操作触屏，播放测试音频，录制一段语音并在停止后回放，读取基础电源状态，最后验证各模块联合运行。

| 需求编号 | 需求 | 首版要求 |
| --- | --- | --- |
| HW-01 | 构建与烧录 | 沿用小智官方固件，固定上游、工具链和依赖版本，提供从环境准备到构建、烧录、启动的可复现步骤。 |
| HW-02 | 启动与诊断 | 支持重复启动；日志能够区分启动、外设初始化与运行异常。 |
| NET-01 | 基础联网 | 沿用所选固件的开发配置入口，支持连接、重启后连接和基本断线恢复，界面显示实际网络状态。 |
| UI-01 | 显示与触控 | 适配显示与触控，提供基础 LVGL 自检界面；触点与操作位置匹配，界面正常刷新并反馈操作结果。 |
| AUD-01 | 音频输出 | 播放已知测试音频，支持播放、停止、音量调整和静音。 |
| AUD-02 | 录音与回放 | 采集麦克风输入，录音结束后本地回放；界面区分空闲、录音、可回放、播放和失败状态。 |
| PWR-01 | 基础电源 | 现有供电方案支持样机运行；完成 BQ25895 识别、通信和基础状态读取。 |
| SYS-01 | 联合运行 | 网络、界面与音频共同运行；失败有日志和状态反馈，不将异常误报为正常。 |

### 录音与回放边界

- 通过触屏开始录音、停止录音和回放；先录后放。
- 仅使用运行期间的临时缓冲，不要求录音持久化或导出。
- 不要求同时录放、语音唤醒或回声消除（AEC）。
- 验收统一使用固定的 5 秒语句，按“录音 → 停止 → 回放”验证。
- 麦克风输入条件尚未确认。若硬件不具备采集条件，AUD-02 标记为阻塞并记录原因，不能用播放预置音频替代录音验收。

## 3. 硬件基线与前置核对

现有硬件包括 ESP32-S3、WM8978、NS4110B、EA2208、EA8101、BQ25895 和 2.4 英寸电容触摸屏，型号信息见 [README 硬件配置](../README.md)。这些信息不能替代实际板卡核对或测量。

| 核对项 | 需要确认和记录的内容 |
| --- | --- |
| 主板与主控 | 板卡版本、Flash 和 PSRAM 容量、相关配置。 |
| 显示与触控 | 显示控制器、触控控制器、分辨率、引脚和总线。 |
| 音频输入 | 麦克风是否存在、实际连接方式、WM8978 输入路径及工作条件；当前均未确认。 |
| 音频输出 | WM8978、功放和扬声器的实际连接、接口与供电条件。 |
| 电源 | 实际供电方式、BQ25895 可读取的状态；区分芯片标称能力与本板实测结果。 |
| 软件基线 | 小智官方固件上游版本、工具链、依赖和板级配置。 |

核对结果形成硬件参数表、引脚表与缺口台账。缺口记录影响的需求、当前证据、阻塞原因和下一步动作；未经核实的接口或参数不作为已知条件。

基础电源状态展示依赖 BQ25895 通信和界面支持，不以电量估算或电流采集作为首版前提。

## 4. 验收标准

全部验收均在目标板上进行，并记录板卡、固件版本、关键配置、测试条件和逐项结果。以下指标尚未验证。

| 对应需求 | 通过条件 |
| --- | --- |
| HW-01 | 另一位团队成员能够仅依文档完成环境准备、构建、烧录、启动和自检。 |
| HW-02、UI-01、NET-01 | 连续执行 10 次重启；每次自检界面、外设初始化结果与网络状态均正确，日志能够区分启动、外设初始化与运行异常。 |
| AUD-01 | 已知测试音频能够正常播放；音量调整、静音和停止操作有效。 |
| AUD-02 | 完成 10 轮“5 秒录音 → 停止 → 回放”；回放内容可辨识、无明显截断，各状态与实际操作一致。 |
| UI-01 | 触控操作与界面位置一致；录音、回放期间仍能响应触控操作并反馈状态。 |
| NET-01 | 完成一次主动断网及恢复；界面正确反馈网络变化，网络恢复后能够重新联网。 |
| PWR-01 | BQ25895 基础状态能够重复读取；读取失败时显示不可用或错误，不展示为正常。 |
| SYS-01 | 保持联网、界面刷新和触控操作，并循环播放测试音频至少 30 分钟；无崩溃、意外重启、卡死或播放中断。 |

未通过或受硬件条件阻塞的项目应保留原始结果、日志和限制说明，不以其他能力通过替代。

## 5. 交付物

- 固件源码、板级配置，以及上游、工具链和依赖版本记录。
- 环境准备、构建、烧录、启动和自检说明。
- 硬件参数表、引脚表与缺口台账。
- 测试步骤、日志、演示证据和按需求编号整理的验收结果。

凭据与个人配置不得提交到仓库或出现在交付日志中。

## 6. 后置范围与后续路线

以下能力不纳入首版验收：完整 AI 对话、触屏配网、完整表情资源、聊天记录持久化、电量估算与校准、电流采集、完整充电控制、自建服务端、音乐资源和模拟器。

| 后续方向 | 范围与边界 |
| --- | --- |
| 完整基础体验 | 在首版硬件路径验证后推进完整 AI 对话、触屏配网、表情资源、聊天记录持久化和进一步的电源能力。 |
| 自建服务扩展 | 先明确 C# 服务端与设备端的职责和接口边界，再推进服务切换、模型选择、在线状态和音乐资源。 |
| 实验探索 | 先验证 Windows 95、NDS 等模拟器的可行性，再决定是否实现虚拟触控手柄等配套交互。 |

以上路线没有日期承诺。实验可以以可行、部分可行或不可行的结论结案；结论不支持的后续实现标记为不适用，以 `not planned` 处理，不记为功能已完成。

## 7. 需求追溯与状态维护

首版具体任务与交付门槛在 [首版 · 硬件适配样机 Milestone](https://github.com/mmsakaito/BYAI/milestone/1) 汇总。跨版本 Epic 不绑定单一版本 Milestone，避免将后续功能计入首版交付。

保留现有 7 个 Epic。首版需求通过不等于整个 Epic 已完成，Epic 中的后续能力仍按各自范围管理。

| 需求或范围 | 对应 Epic | 实施与验收 Issue |
| --- | --- | --- |
| HW-01 | [#1 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1) | [#8](https://github.com/mmsakaito/BYAI/issues/8)；[#43](https://github.com/mmsakaito/BYAI/issues/43) |
| HW-02 | [#1 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1) | [#9](https://github.com/mmsakaito/BYAI/issues/9)、[#11](https://github.com/mmsakaito/BYAI/issues/11) |
| NET-01 | [#1 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1) | [#10](https://github.com/mmsakaito/BYAI/issues/10) |
| UI-01 | [#3 屏幕与交互](https://github.com/mmsakaito/BYAI/issues/3) | [#16](https://github.com/mmsakaito/BYAI/issues/16)、[#17](https://github.com/mmsakaito/BYAI/issues/17) |
| AUD-01 | [#2 音频驱动](https://github.com/mmsakaito/BYAI/issues/2) | [#12](https://github.com/mmsakaito/BYAI/issues/12)、[#13](https://github.com/mmsakaito/BYAI/issues/13)、[#14](https://github.com/mmsakaito/BYAI/issues/14)、[#15](https://github.com/mmsakaito/BYAI/issues/15) |
| AUD-02 | [#2 音频驱动](https://github.com/mmsakaito/BYAI/issues/2) | [#39](https://github.com/mmsakaito/BYAI/issues/39)；[#40](https://github.com/mmsakaito/BYAI/issues/40)；[#41](https://github.com/mmsakaito/BYAI/issues/41)；[#15](https://github.com/mmsakaito/BYAI/issues/15) |
| PWR-01 | [#5 电源管理](https://github.com/mmsakaito/BYAI/issues/5) | [#24](https://github.com/mmsakaito/BYAI/issues/24)、[#27](https://github.com/mmsakaito/BYAI/issues/27) |
| SYS-01 | [#1 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1) | [#42](https://github.com/mmsakaito/BYAI/issues/42)；[#43](https://github.com/mmsakaito/BYAI/issues/43) |
| 后续完整电源管理 | [#5 电源管理](https://github.com/mmsakaito/BYAI/issues/5) | [#25 电量估算](https://github.com/mmsakaito/BYAI/issues/25)、[#26 电流采集](https://github.com/mmsakaito/BYAI/issues/26)、[#44 充电控制与综合状态](https://github.com/mmsakaito/BYAI/issues/44)，不纳入首版验收。 |
| 后续 AI 与数据存储 | [#4 AI 与数据存储](https://github.com/mmsakaito/BYAI/issues/4) | 不纳入首版验收。 |
| 后续自建服务扩展 | [#6 自建 C# 服务端](https://github.com/mmsakaito/BYAI/issues/6) | 不纳入首版验收。 |
| 后续实验探索 | [#7 进阶探索](https://github.com/mmsakaito/BYAI/issues/7) | 不纳入首版验收。 |

- PRD 定义需求、范围和验收目标；变更范围时同步更新相关需求与 Issue。
- Issue 记录实施、依赖、风险、验收证据和结论；音频 Epic 应包含首版录音与回放范围。
- 音频、界面与电源的实施依赖关联到构建、启动、联网等具体任务，避免整体依赖包含整机验收任务的 #1 Epic 而形成循环。
- Project 维护动态状态。遇到未解决的硬件、服务或决策依赖时标记阻塞并说明原因；只有验收通过且交付记录齐全时，才标记对应任务完成。
