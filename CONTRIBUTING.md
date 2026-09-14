# 参与贡献

感谢参与 BYAI 小智。项目围绕 ESP32-S3、小智基础适配、音频、触屏交互、服务端和进阶实验推进；每项工作都应有可追溯的目标、验证方式和结果记录。

## 开始前

1. 先检查现有 Issue、Project 状态和相关模块，避免重复工作。
2. 新工作优先作为现有 Epic 的子 Issue；跨模块或新增能力再创建顶层 Issue。
3. 使用 [Issue 模板](.github/ISSUE_TEMPLATE/task.md) 建立任务，写明目标、范围、验收标准、依赖与风险。

## Issue 规范

标题格式：

```text
[类型(范围)] 中文任务名
```

常用类型：

```text
feat | hw | build | config | design | research | spike | test | perf | fix | refactor | docs | ci | chore
```

范围：

```text
bringup | audio | display | touch | ui | wifi | ai | storage | power | server | emulator
```

示例：

```text
[feat(audio)] WM8978 音频输出
[hw(power)] BQ25895 通信与充电控制
[spike(emulator)] NDS 模拟器运行可行性验证
```

`research` 用于技术选型或资料调研；`spike` 用于有明确实验边界且必须产出可行、部分可行或不可行结论的探索。

## Project 状态

顶层 Epic 作为 Project 卡片，子 Issue 用于执行和验收。状态按以下含义维护：

| 状态 | 使用时机 |
| --- | --- |
| 待开始 | 目标明确但尚未动工 |
| 进行中 | 正在实施或联调 |
| 阻塞 | 等待硬件、服务、决策或外部条件 |
| 待验证 | 实现完成，等待构建、设备或服务端验证 |
| 已完成 | 验收通过，交付记录已补全 |

## 开发与验证

1. 分支名使用小写英文和连字符，例如 `feat/audio-output`、`fix/wifi-reconnect`。
2. 修改硬件相关能力时，记录测试板、固件版本、关键配置和验证结果。
3. 调研与实验任务需要保留步骤、数据、日志、截图或录屏，并在 Issue 中写出结论。
4. 出现未解决的依赖或风险时，将对应 Project 卡片改为“阻塞”，说明原因和需要的外部条件。

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
bringup | audio | display | touch | ui | wifi | ai | storage | power | server | emulator
```

示例：

```text
feat(audio): 实现 WM8978 I2S 音频输出
hw(power): 验证 BQ25895 I2C 通信
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

PR 标题沿用 Issue 格式：

```text
[feat(audio)] WM8978 音频输出
```

使用 [PR 模板](.github/PULL_REQUEST_TEMPLATE.md)，并至少完成：

- 关联对应 Issue；
- 概述实际变更；
- 记录构建、设备或服务端验证；
- 说明影响、限制和证据；
- 在合并后更新关联 Issue 与 Project 状态。
