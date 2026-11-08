# 参与贡献

感谢参与 BYAI 小智。项目围绕 ESP32-S3、音频、触屏交互和后续 AI 能力推进；工作目标、实际结果和未解决问题应便于团队查找。

## 开始前

1. 先检查现有 Issue、Project 状态和相关模块，避免重复工作。
2. 应用功能用主 Issue 说明目标；板级、驱动和需要分配的具体工作作为直接子 Issue，保持两级。执行子 Issue 加入 Project 分配负责人和进度，小改动不必另建 Issue。
3. 根据工作选择[功能](.github/ISSUE_TEMPLATE/feature.yml)、[硬件与驱动](.github/ISSUE_TEMPLATE/hardware.yml)或[缺陷](.github/ISSUE_TEMPLATE/bug.yml)模板；其他事项可以自由填写。

## Issue 规范

标题格式：

```text
类型(范围): 中文任务名
```

常用类型：

```text
feat | hw | fix | docs
```

范围：

```text
repo | bringup | audio | display | touch | ui | wifi | ai | storage | power | server | emulator
```

示例：

```text
feat(audio): WM8978 音频输出
hw(power): BQ25890H 基础通信
spike(emulator): NDS 模拟器运行可行性验证
```

调研或实验可用 `research`、`spike`；其他常见 Conventional Commit 类型也可按需使用。标题说明工作内容即可，不必为了分类拆出新任务。

## Project 状态

Project 用于分配执行子 Issue 和查看进度；功能主 Issue 汇总目标与结果。状态按以下含义维护：

| 状态 | 使用时机 |
| --- | --- |
| 待开始 | 目标明确但尚未动工 |
| 进行中 | 正在实施或联调 |
| 阻塞 | 等待硬件、服务、决策或外部条件 |
| 待验证 | 实现完成，等待构建、设备或服务端验证 |
| 已完成 | 目标已达到，实际结果已记录 |

## 开发与验证

1. 按改动内容做必要的构建或功能检查，简要记录实际结果；不要求每个 Issue 预先填写完整测试流程。
2. 测试记录与结果在 PR 评论中简要说明，不写入仓库文档或 README，不填写测试机路径。涉及实板时说明板卡、固件版本、关键条件和实际现象，不把构建通过写成实板通过。架构设计文档保持精简，便于查询。
3. 调研或探索写清结论和主要依据；卡住时在 Project 中标记“阻塞”并说明原因。

## 分支规范

分支流向为：`功能分支 → PR → dev → PR → main`。

- `main` 是稳定分支，禁止直接 `push`；只接受 Pull Request 合并。
- `dev` 是日常集成基线。常规开发禁止直接 `push` 到 `dev`，功能分支通过 PR 合入 `dev`。
- 只有紧急修复可以直接推送到 `dev`。此类提交必须已签名、完成必要验证，并在提交正文关联对应 Issue；随后仍需通过 PR 将 `dev` 合入 `main`。
- 新功能、修复、调研、验证和重构都从最新 `dev` 创建独立分支，不直接在 `main` 或 `dev` 上开发。

分支名使用小写英文、连字符和 Conventional Commits 类型：

```text
类型/范围-简短描述
```

示例：

```text
feat/audio-output
hw/wm8978-clock
config/wifi-provisioning
fix/wifi-reconnect
research/storage-backend
spike/nds-emulator
test/audio-output
```

创建分支前先同步 `dev`：

```bash
git switch dev
git pull --ff-only origin dev
git switch -c feat/audio-output
```

常规功能分支的 PR 目标为 `dev`；`main` 的 PR 应来自已验证的 `dev` 集成结果。

## 提交规范

提交信息使用 Conventional Commits 格式：

```text
类型(范围): 中文摘要
```

类型和范围与 Issue 标题保持一致，但提交信息不使用方括号。

```text
feat | hw | build | config | design | research | spike | test | perf | fix | refactor | docs | ci | chore
```

```text
repo | bringup | audio | display | touch | ui | wifi | ai | storage | power | server | emulator
```

示例：

```text
feat(audio): 实现 WM8978 I2S 音频输出
hw(power): 验证 BQ25890H I2C 通信
build(bringup): 固定 ESP-IDF 工具链版本
spike(emulator): 记录 NDS 模拟器可行性结果
fix(wifi): 修复重连后网络配置丢失
docs(server): 补充 C# 服务端接入说明
```

- 一个提交只完成一个可独立理解的意图；不要把功能、格式整理和无关文件混在一起。
- 摘要使用动词开头，说明实际变化，不以句号结尾。
- 需要补充动机、实现取舍、硬件条件或验证结果时，在标题后空一行写正文。
- 提交关联 Issue 时，在正文末尾使用 `Refs #编号`；只有确认合并该提交即可关闭任务时才使用 `Closes #编号`。
- 所有推送用于合并的提交必须签名。先将可验证的 GPG、SSH 或 S/MIME 签名密钥关联到 GitHub，再使用 `git commit -S` 创建提交。
- 修改最近一次提交时也必须重新签名：`git commit --amend -S`。
- 推送前使用 `git log --show-signature -1` 确认最新提交的签名有效。

```bash
git commit -S -m "feat(audio): 实现 WM8978 I2S 音频输出"
```

示例正文：

```text
feat(audio): 实现 WM8978 I2S 音频输出

- 初始化 I2S DMA 输出链路
- 配置 DAC 与扬声器输出路由
- 在目标板播放 16 kHz 测试音频

Refs #14
```

## Pull Request

PR 标题使用 Conventional Commits 格式，不使用方括号：

```text
feat(audio): WM8978 音频输出
```

使用 [PR 模板](.github/PULL_REQUEST_TEMPLATE.md)，并至少完成：

- 关联对应 Issue；
- 概述实际变更；
- 在 PR 评论中简要记录构建、设备或服务端验证，正文引用评论链接；
- 说明影响、限制和证据；
- 在合并后更新关联 Issue 与 Project 状态。
