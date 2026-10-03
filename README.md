<div align="center">

<img src="LOGO.png" alt="BYAI 小智" width="180">

# BYAI 小智

**基于 ESP32-S3 的 AI 语音交互终端 · 首版硬件适配样机**

硬件适配 · 触屏自检 · 录音回放 · 联合验证

> 🚀 先模仿，后超越。

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![UI](https://img.shields.io/badge/UI-LVGL-00979D?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-规划与探索-FFB300?style=for-the-badge)

### [📖 开发环境安装说明](docs/development-environment.md)

</div>

---

## ✨ 项目简介

**BYAI 小智** 是一个基于 **ESP32-S3** 的软硬件探索项目。

首版从 ESP-IDF 例程搭建固件骨架，先逐步接入并联调现有板卡模块，再适配小智。硬件按“首板装配与稳定上电 → 各模块测试固件验证 → 制作多块同版样机并分发团队”推进；具体步骤和交付要求见 [PRD](docs/PRD.md)。

首版范围包括构建与启动、基础联网、显示与触控、音频输出、录音与回放、基础电源和联合运行。新接手项目时，先读 [PRD：首版要做什么、接下来怎么推进](docs/PRD.md)，了解当前基础、第一轮工作、任务顺序和完成标准。

开发环境的初始安装流程见 [开发环境搭建](docs/development-environment.md)。

**VS Code 打开方式：** 使用“文件 → 打开文件夹”直接打开 [`firmware/`](firmware/)，使该子目录成为工作区根目录。首次打开后，在命令面板运行 `ESP-IDF: Select Current ESP-IDF Version`，选择本机安装的 `v6.1`，再运行 `ESP-IDF: Doctor Command` 检查配置。ESP-IDF 扩展需要识别子目录中的 `CMakeLists.txt`；只打开 BYAI 仓库根目录时，无法将该例程作为当前 ESP-IDF 工程使用。

**先把基础做扎实，再把想象力装进去。**

> [!NOTE]
> `firmware/` 已包含 `esp_wifi_service` 起步例程，尚未完成 BYAI 板级适配和实板验证。上述产品能力仍为待实现、待验证的目标。PRD 定义需求与验收目标；实施进度、验收证据和技术结论以 GitHub Project 与 Issue 为准。

## 🛠️ 硬件配置

| 模块 | 型号 / 规格 | 用途 |
| :--- | :--- | :--- |
| 🧠 主控模组 | **ESP32-S3-WROOM-1-N16R8**（U4） | 核心控制与应用运行 |
| 💾 外部 Flash | **W25Q128JVPIQ**（U5） | 原理图标注的外部存储器 |
| 🎵 音频编解码器 | **WM8978GEFL/RV**（U6） | 音频采集与播放电路 |
| 🎙️ 麦克风 | **LMA2718B381-OAK02**（U10） | 原理图标注的 MEMS 麦克风 |
| 🔊 功放芯片 | **NS4110B**（U8） | 音频功率放大 |
| ⚡ 降压芯片 | **EA2208T6R**（U1） | 降压供电 |
| 🔋 升压芯片 | **EA8101T5R**（U3） | 升压供电 |
| 🔌 电池充电芯片 | **BQ25890RTWT**（U2） | 电池充电管理 |
| 🖥️ 触摸屏 | **2.4 英寸电容触摸屏** | 原理图标出 FPC1（16 pin）；屏幕/触控控制器型号和接口细节待补 |
| 🔌 板上接口 | 2 × USB Type-C、TF 卡座、3.5 mm 耳机座 | 原理图可见；引脚用途和实机功能仍按板级资料核对 |

以上型号依据 `20261001` 工程原理图 P1/P2 记录；原理图标注不代表实物装配、连线或功能已经验证。

## 🧭 项目管理

开发路线在 [BYAI · 开发路线 Project](https://github.com/users/mmsakaito/projects/1) 中维护。

- 顶层 Epic 作为 Project 卡片，反映模块状态与优先级；首版具体任务和验收门槛见 [首版 Milestone](https://github.com/mmsakaito/BYAI/milestone/1)。
- 每个 Epic 下的子 Issue 管理具体实施、验证、风险和交付记录。
- Issue 与 PR 标题统一使用 `类型(范围): 中文任务名`，例如 `feat(audio): WM8978 音频输出`。
- 贡献、分支、提交签名和 PR 规范见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 🗺️ 开发路线

| 阶段 | 目标 | Epic Issue |
| :--- | :--- | :--- |
| 首版 · 硬件适配样机 | 按 [PRD](docs/PRD.md) 验证主要硬件路径、制作并分发同版样机、完成可复现交付与联合运行；小智适配排在模块验证和整机联调之后。 | [#1 小智基础适配](https://github.com/mmsakaito/BYAI/issues/1)<br>[#2 音频驱动](https://github.com/mmsakaito/BYAI/issues/2)<br>[#3 屏幕与交互](https://github.com/mmsakaito/BYAI/issues/3)<br>[#5 电源管理](https://github.com/mmsakaito/BYAI/issues/5) |
| 后续 · 完整基础体验 | 推进完整 AI 对话、触屏配网、表情资源、聊天记录持久化和进一步的电源能力。 | [#2 音频驱动](https://github.com/mmsakaito/BYAI/issues/2)<br>[#3 屏幕与交互](https://github.com/mmsakaito/BYAI/issues/3)<br>[#4 AI 与数据存储](https://github.com/mmsakaito/BYAI/issues/4)<br>[#5 电源管理](https://github.com/mmsakaito/BYAI/issues/5) |
| 后续 · 自建服务扩展 | 先明确 C# 服务端边界，再推进服务切换、模型选择、在线状态和音乐资源。 | [#6 自建 C# 服务端](https://github.com/mmsakaito/BYAI/issues/6) |
| 后续 · 实验探索 | 先验证 Windows 95、NDS 等模拟器的可行性，再决定虚拟触控手柄等配套交互。 | [#7 进阶探索](https://github.com/mmsakaito/BYAI/issues/7) |

首版需求通过不等于整个 Epic 已完成。后续路线不承诺日期；实验探索必须记录测试环境、资源占用、运行证据和可行、部分可行或不可行的结论。

---

<div align="center">

**从一块开发板开始，让小智拥有更多可能。**

⚡ **BYAI 小智 · 先模仿，后超越** ⚡

</div>
