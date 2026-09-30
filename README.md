<div align="center">

# 🤖 BYAI 小智

**基于 ESP32-S3 的 AI 语音交互终端 · 首版硬件适配样机**

硬件适配 · 触屏自检 · 录音回放 · 联合验证

> 🚀 先模仿，后超越。

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![UI](https://img.shields.io/badge/UI-LVGL-00979D?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-规划与探索-FFB300?style=for-the-badge)

</div>

---

## ✨ 项目简介

**BYAI 小智** 是一个基于 **ESP32-S3** 的软硬件探索项目。

首版面向团队自用验证：沿用小智官方固件，在现有硬件上建立可复现构建、稳定运行、主要硬件路径可验证的样机，为下一版完整 AI 对话体验打基础。

首版范围包括构建与启动、基础联网、显示与触控、音频输出、录音与回放、基础电源和联合运行。详细需求、硬件核对项、验收标准与后置范围见 [PRD：首版硬件适配样机](docs/PRD.md)。

**先把基础做扎实，再把想象力装进去。**

> [!NOTE]
> 当前仓库尚未提交固件、构建系统或自动测试，上述能力均为待实现、待验证的目标。PRD 定义需求与验收目标；实施进度、验收证据和技术结论以 GitHub Project 与 Issue 为准。

## 🛠️ 硬件配置

| 模块 | 型号 / 规格 | 用途 |
| :--- | :--- | :--- |
| 🧠 主控 | **ESP32-S3** | 核心控制与应用运行 |
| 🎵 音频芯片 | **WM8978** | I2S 音频输入 / 输出 |
| 🔊 功放芯片 | **NS4110B** | 音频功率放大，标称最高 15 W |
| ⚡ 降压芯片 | **EA2208** | 3.3 V 降压供电 |
| 🔋 升压芯片 | **EA8101** | 升压供电 |
| 🔌 电池充电芯片 | **BQ25895** | 5 A 电池充电管理 |
| 🖥️ 触摸屏 | **2.4 英寸电容触摸屏** | 界面显示与触控交互 |

## 🧭 项目管理

开发路线在 [BYAI · 开发路线 Project](https://github.com/users/mmsakaito/projects/1) 中维护。

- 顶层 Epic 作为 Project 卡片，反映模块状态与优先级；首版具体任务和验收门槛见 [首版 Milestone](https://github.com/mmsakaito/BYAI/milestone/1)。
- 每个 Epic 下的子 Issue 管理具体实施、验证、风险和交付记录。
- Issue 与 PR 标题统一使用 `类型(范围): 中文任务名`，例如 `feat(audio): WM8978 音频输出`。
- 贡献、分支、提交签名和 PR 规范见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 🗺️ 开发路线

| 阶段 | 目标 | Epic Issue |
| :--- | :--- | :--- |
| 首版 · 硬件适配样机 | 按 [PRD](docs/PRD.md) 验证主要硬件路径、可复现交付与联合运行。 | [#1 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1)<br>[#2 音频驱动](https://github.com/mmsakaito/BYAI/issues/2)<br>[#3 屏幕与交互](https://github.com/mmsakaito/BYAI/issues/3)<br>[#5 电源管理](https://github.com/mmsakaito/BYAI/issues/5) |
| 后续 · 完整基础体验 | 推进完整 AI 对话、触屏配网、表情资源、聊天记录持久化和进一步的电源能力。 | [#2 音频驱动](https://github.com/mmsakaito/BYAI/issues/2)<br>[#3 屏幕与交互](https://github.com/mmsakaito/BYAI/issues/3)<br>[#4 AI 与数据存储](https://github.com/mmsakaito/BYAI/issues/4)<br>[#5 电源管理](https://github.com/mmsakaito/BYAI/issues/5) |
| 后续 · 自建服务扩展 | 先明确 C# 服务端边界，再推进服务切换、模型选择、在线状态和音乐资源。 | [#6 自建 C# 服务端](https://github.com/mmsakaito/BYAI/issues/6) |
| 后续 · 实验探索 | 先验证 Windows 95、NDS 等模拟器的可行性，再决定虚拟触控手柄等配套交互。 | [#7 进阶探索](https://github.com/mmsakaito/BYAI/issues/7) |

首版需求通过不等于整个 Epic 已完成。后续路线不承诺日期；实验探索必须记录测试环境、资源占用、运行证据和可行、部分可行或不可行的结论。

---

<div align="center">

**从一块开发板开始，让小智拥有更多可能。**

⚡ **BYAI 小智 · 先模仿，后超越** ⚡

</div>
