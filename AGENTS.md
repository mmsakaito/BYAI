# BYAI Repository Guide

## 项目概况

- BYAI 是基于 ESP32-S3 的 AI 语音交互终端项目。
- 当前仓库仍是文档与规划基线，固件源码、构建系统、自动化测试和硬件资源尚未提交。
- 项目目标、首版范围和验收边界见 [`README.md`](README.md) 与 [`docs/PRD.md`](docs/PRD.md)。
- 开发环境安装步骤见 [`docs/development-environment.md`](docs/development-environment.md)，可复用的 EIM 配置见 [`config.toml`](config.toml)。
- 使用 ESP-IDF 官方文档时，优先参考乐鑫文档；需要检索官方资料时可使用 ESP 文档 MCP。

## 开发环境

- 当前统一配置为 ESP32-S3、ESP-IDF `v6.1`，通过 EIM 导入 `config.toml` 安装。
- 配置文件不固定本机安装路径；在其他电脑导入前，确认目标系统、镜像可达性和 EIM 依赖检查结果。
- 同一次构建必须使用同一套 ESP-IDF、工具目录和 Python 环境。不要把系统 Python、Conda Python 或另一套 ESP-IDF 工具混入当前环境。
- EIM 安装完成后，在 VS Code 中运行 `ESP-IDF: Select Current ESP-IDF Version` 选择版本，再运行 `ESP-IDF: Doctor Command` 检查环境。
- 如果 VS Code 未发现安装，检查 EIM 的 `eim_idf.json` 路径；默认位置为 Windows 的 `C:\Espressif\tools\eim_idf.json` 或 Linux/macOS 的 `$HOME/.espressif/tools/eim_idf.json`。
- `config.toml` 中的 `python_version_override = "python313"` 是安装偏好，不代表所有系统都已完成验证；实际安装后应记录 EIM、ESP-IDF、Python 和扩展版本。

## 构建、烧录与测试

- 当前仓库没有 `CMakeLists.txt`、固件源码或测试套件，因此不能声称 BYAI 已完成构建或硬件验证。
- 文档变更至少运行：

  ```bash
  git diff --check
  git status -sb
  ```

- ESP-IDF 接入后，必须在 README 和对应 Issue 中记录准确的配置、构建、烧录、监视和测试命令，并注明测试板、固件版本、关键配置、日志和结果。
- 构建验证、静态检查或示例工程构建不能替代目标板验证；音频、显示、触控、Wi-Fi、电源和服务功能都要记录实际设备证据。
- 调研或实验任务保留可复现步骤、输入、日志和结论，并明确可行、部分可行或不可行。

## 项目结构

- 仓库级说明放在 `README.md`，协作规则放在 `CONTRIBUTING.md`，需求和路线文档放在 `docs/`。
- 固件接入后，按板级配置、驱动、应用、测试和资源划分清晰的顶层目录，并同步更新 README。
- 构建产物、下载依赖、工具缓存和本机 IDE 设置不提交；`.vscode/` 已由 `.gitignore` 忽略。

## 编码与文档规范

- Markdown 使用 UTF-8、描述性标题和面向任务的短列表，沿用被编辑文件的现有风格。
- 固件语言和构建工具确定前，不新增格式化或 lint 规则。
- C/C++ 固件接入后遵循 ESP-IDF 风格；公开接口、硬件假设、资源所有权、错误处理和并发边界应写清设计意图。
- 文档中的版本、路径、命令和硬件结论必须来自实际检查或官方资料；不要把个人机器路径写成团队默认路径。

## Git、提交与 Pull Request

- 常规工作流为：`功能分支 → PR → dev → PR → main`。新工作从最新 `dev` 创建独立分支。
- 分支名使用小写 Conventional Commit 类型和连字符，例如 `docs/development-environment`、`fix/wifi-reconnect`。
- 提交使用签名 Conventional Commit，例如 `docs(repo): 补充仓库说明`；提交前运行 `git diff --cached --check` 和 `git diff --cached`。
- 功能分支 PR 默认合入 `dev`；经过验证的 `dev` 再通过 PR 合入 `main`。`main` 不直接推送。
- PR 使用 `.github/PULL_REQUEST_TEMPLATE.md`，说明变更、关联 Issue、验证、影响和证据。标题应能直接作为合并提交标题。
- 合并后同步本地分支前，先运行 `git fetch --prune origin`，再用 `git branch -vv` 和远端日志确认状态。

## 边界

### 始终执行

- 开始前检查当前分支、`git status`、目标文件和相关 Issue；发现同一文件有并行修改时先说明。
- 提交前确认暂存范围，只包含本次任务相关文件；保留用户已有改动。
- 对硬件变更记录开发板、固件版本、配置、命令、日志和实际结果。

### 需要先确认

- 擦除 NVS、擦除 Flash、烧录设备、运行可能改变设备数据的完整硬件测试、推送远端、创建或合并 PR，以及重写远端历史。
- 修改已有硬件引脚、电源、网络凭据或持久化数据前，先读取现有文档和实现，说明影响范围。

### 不要执行

- 不要提交他人的本机路径、凭据、密钥或未验证的硬件结论。
- 不要混用不同 ESP-IDF、工具链和 Python 环境，也不要把构建产物或工具缓存提交到仓库。
- 不要覆盖、回退或删除用户未授权的修改；不要直接向 `main` 提交或推送。
