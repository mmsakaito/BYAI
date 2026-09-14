<div align="center">

# 🤖 BYAI 小智

**基于 ESP32-S3 的 AI 语音交互终端**

语音交互 · 触屏操作 · 电源管理 · 模拟器探索

> 🚀 先模仿，后超越。

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![UI](https://img.shields.io/badge/UI-LVGL-00979D?style=for-the-badge)
![Server](https://img.shields.io/badge/Server-C%23-512BD4?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-规划与探索-FFB300?style=for-the-badge)

</div>

---

## ✨ 项目简介

**BYAI 小智** 是一个基于 **ESP32-S3** 的软硬件探索项目。

项目先完成小智官方固件在目标硬件上的适配，再推进音频驱动、触屏交互、聊天记录存储和电源管理；随后探索自建 C# 服务端、模型与资源管理，以及经典系统和游戏模拟器。

**先把基础做扎实，再把想象力装进去。**

> [!NOTE]
> 实施进度、验收证据和技术结论以 GitHub Project 与 Issue 为准。README 只保留项目定位、硬件信息和路线入口。

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

- 顶层 Epic 作为 Project 卡片，反映模块状态、优先级和 Milestone。
- 每个 Epic 下的子 Issue 管理具体实施、验证、风险和交付记录。
- Issue 与 PR 标题统一使用 `[类型(范围)] 中文任务名`，例如 `[feat(audio)] WM8978 音频输出`。
- 日常开发从 `dev` 创建 `feat/audio-output` 等功能分支，经 PR 合入 `dev`；`main` 只接受 PR。
- 贡献、分支、提交签名和 PR 规范见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 🗺️ 开发路线

| 阶段 | 目标 | Epic Issue |
| :--- | :--- | :--- |
| 阶段 0 · 小智基础适配 | 在目标 ESP32-S3 上建立可构建、烧录、启动和联网的小智固件基线。 | [\[feat(bringup)\] 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1) |
| 第一阶段 · 基础功能 | 完成音频、屏幕与交互、官网模型服务与数据存储、电源管理。 | [\[feat(audio)\] 音频驱动](https://github.com/mmsakaito/BYAI/issues/2)<br>[\[feat(ui)\] 屏幕与交互](https://github.com/mmsakaito/BYAI/issues/3)<br>[\[feat(ai)\] AI 与数据存储](https://github.com/mmsakaito/BYAI/issues/4)<br>[\[feat(power)\] 电源管理](https://github.com/mmsakaito/BYAI/issues/5) |
| 第二阶段 · 自建服务端 | 对接 C# 服务端，支持服务切换、模型选择、在线状态和音乐资源。 | [\[feat(server)\] 自建 C# 服务端](https://github.com/mmsakaito/BYAI/issues/6) |
| 第三阶段 · 进阶探索 | 以可复现实验验证 Windows 95、NDS 等模拟器及虚拟触控手柄的可行性。 | [\[spike(emulator)\] 进阶探索](https://github.com/mmsakaito/BYAI/issues/7) |

进阶探索必须记录测试环境、资源占用、运行证据和可行、部分可行或不可行的结论。

---

<div align="center">

**从一块开发板开始，让小智拥有更多可能。**

⚡ **BYAI 小智 · 先模仿，后超越** ⚡

</div>
