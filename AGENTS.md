# BYAI Agent Guide

## Overview

- BYAI 是基于 ESP32-S3 的 AI 语音交互终端项目。
- 固件从 `firmware/` 中的 `esp_wifi_service` 例程起步；板级适配、外设联调和实板验证仍待完成。
- 面向成员的开发环境安装入口见 [`docs/development-environment.md`](docs/development-environment.md)；项目说明和协作规则分别见 [`README.md`](README.md) 与 [`CONTRIBUTING.md`](CONTRIBUTING.md)。
- ESP 文档 MCP 用于检索乐鑫官方技术文档；配置和使用说明见 <https://mcp.espressif.com/docs>。

## Bootstrap

- 仅当本机尚未配置 ESP-IDF 开发环境时，使用 [`config.toml`](config.toml) 通过 [ESP-IDF Installation Manager](https://docs.espressif.com/projects/idf-im-ui/en/latest/) 导入安装；当前配置目标为 ESP32-S3 与 ESP-IDF `v6.1`。
- 配置文件不固定本机安装路径；在其他电脑导入前，确认目标系统、镜像可达性和 EIM 依赖检查结果。

## Local environment configuration

- `ENV.local` 是每台开发机独立维护的本地配置，不由构建过程自动生成，也不提交到 Git；首次创建或 SDK/EIM 安装变更后，应根据本机实际安装结果填写并核对，不要填写尚未存在的预期路径。
- 复制 [`ENV.local.example`](ENV.local.example) 为 `ENV.local`，维护 `ESP_IDF_PATH`、`IDF_TOOLS_PATH` 和 `IDF_PYTHON_ENV_PATH` 三个路径；三者必须来自同一套 ESP-IDF/EIM 安装。
- `ESP_IDF_PATH` 必须指向包含 `tools/idf.py` 的 SDK 根目录；激活脚本的位置和命名取决于安装方式。EIM 安装使用 `IDF_TOOLS_PATH` 下按平台生成的脚本，传统安装才使用 SDK 根目录下的 `export.*` 脚本。
- `IDF_TOOLS_PATH` 必须指向该 SDK/EIM 安装实际使用的工具目录；`IDF_PYTHON_ENV_PATH` 必须指向同一安装提供的 Python 环境，Unix-like 系统应存在 `bin/python`，Windows 应存在 `python.exe`。
- 更新 SDK、EIM 安装或 Python 环境后，重新核对并成组更新这三个路径，再按当前 shell 的平台命令激活；激活失败时先检查路径和脚本存在性，不要混用系统 Python、Conda Python 或其他 ESP-IDF 环境。
- 维护 `ENV.local` 时保留已有本机条目，只新增或更新必要键；使用 `KEY="VALUE"`、正斜杠和注释，不写命令替换、函数、路径追加或其他可执行语句。

## Commands

### Unix-like systems (Linux/macOS/WSL)

- Activate (EIM): 在仓库根目录运行 `set -a; . ./ENV.local; set +a`，然后 source `"$IDF_TOOLS_PATH/activate_idf_${ESP_IDF_VERSION}.sh"`；Fish 使用 `source "$IDF_TOOLS_PATH/activate_idf_${ESP_IDF_VERSION}.fish"`。Linux 和 macOS 的 EIM 命名规范相同，不要改用 Windows 的 PowerShell profile 脚本。
- Activate (传统安装): 如果不是 EIM 安装，Unix-like 系统才使用 `. "$ESP_IDF_PATH/export.sh"`。不要直接执行激活脚本；必须使用 `source`/`.` 使环境变量留在当前 shell。
- Build: 从仓库根目录激活环境后进入 `firmware/` 应用根目录，运行 `IDF_PYTHON="$IDF_PYTHON_ENV_PATH/bin/python"`，然后 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" build`。
- Clean: Python 路径不一致时运行 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" fullclean` 后再构建。
- Target / flash: `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" set-target esp32s3`；使用 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" -p PORT flash monitor` 烧录并监视。

### Windows

- Activate (EIM, PowerShell): 从 `ENV.local` 读取并设置路径，使用 `IDF_TOOLS_PATH\Microsoft.<version>.PowerShell_profile.ps1`；例如 ESP-IDF 6.1 使用 `Microsoft.v6.1.PowerShell_profile.ps1`。必须在当前 PowerShell 中 dot-source：`. "$env:IDF_TOOLS_PATH\Microsoft.v6.1.PowerShell_profile.ps1"`。
- Activate (EIM, CMD): EIM 只有在配置 `create_bat_activation_script = true` 时才生成 Windows `.bat` 激活脚本；存在时使用 `call` 调用。没有该文件时使用 PowerShell，不要假设 EIM 一定提供 CMD 脚本。
- Activate (传统安装): 非 EIM 安装的 PowerShell/CMD 才使用 SDK 根目录下的 `export.ps1`/`export.bat`。
- Build (PowerShell): 从仓库根目录激活环境后进入 `firmware/`，运行 `$IDF_PYTHON = Join-Path $env:IDF_PYTHON_ENV_PATH "python.exe"`，然后 `& $IDF_PYTHON "$env:IDF_PATH/tools/idf.py" build`。
- Build (CMD): 从仓库根目录激活环境后进入 `firmware\`，运行 `set "IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\python.exe"`，然后 `"%IDF_PYTHON%" "%IDF_PATH%\tools\idf.py" build`。
- Clean / target / flash: 使用当前 shell 中对应的 `IDF_PYTHON`，分别运行 `fullclean`、`set-target esp32s3` 或 `-p PORT flash monitor`。

## Code Style

- Markdown 使用 UTF-8、描述性标题和面向任务的短列表，沿用被编辑文件的现有风格。
- 固件接入后，C/C++ 遵循 ESP-IDF 风格，使用四空格缩进、`snake_case`、`CONFIG_*`、`static const char *TAG` 与 `ESP_LOG*`；公开接口保持精简，初始化和 I/O 优先返回 `esp_err_t`。
- 文件：嵌入式代码按板级配置、驱动、应用、测试和资源划分清晰的顶层目录；板级引脚定义集中管理，只格式化本次修改的文件。
- 注释：公开接口、模块入口、复杂内部函数使用 Doxygen 风格注释，说明参数、返回值、并发/锁、资源所有权、错误回退、持久化和硬件假设，不逐行复述代码。
- 固件语言和构建工具确定前，不新增格式化或 lint 规则。

## Pull Requests

- 创建或更新 PR 前，读取 [PR 模板](.github/PULL_REQUEST_TEMPLATE.md)，并以实际 diff、提交记录和验证结果填写；标题应能直接作为合并提交标题，正文保留变更动机、影响和实际验证。
- 功能分支保留便于开发和审查的原子提交；不得为了 Squash 对已共享分支执行 `rebase`、`reset` 或强推。批准并完成适用的人工核验后，由维护者在 GitHub 使用 **Squash and merge**。
- 常规工作流为：`功能分支 → PR → dev → PR → main`。功能分支 PR 默认合入 `dev`；经过验证的 `dev` 再通过 PR 合入 `main`，`main` 不直接推送。
- 提交使用签名 Conventional Commit，例如 `docs(repo): 补充仓库说明`；提交前运行 `git diff --cached --check` 和 `git diff --cached`。
- 开 PR 后，在交付消息中提醒用户该 PR 应使用 **Squash and merge**；Code Review 后说明审查结论、已处理或仍未处理的意见。

## Boundaries

### Always

- 开始前检查当前分支、`git status` 和目标文件；发现同一模块并行修改时先报告并协调。
- `ENV.local` 仅记录本机非敏感路径，且必须符合“Local environment configuration”中的生成、校验和维护规则。
- 提交前运行 `git diff --cached --check` 和 `git diff --cached`；提交使用 `git commit -S`。分支、Issue、PR、审查和合并规则以 [`CONTRIBUTING.md`](CONTRIBUTING.md) 及 [Issue](.github/ISSUE_TEMPLATE/) / [PR](.github/PULL_REQUEST_TEMPLATE.md) 模板为准。
- 对硬件变更记录开发板、固件版本、配置、命令、日志和实际结果。

### Ask First

- 擦除 NVS、擦除 Flash、运行完整硬件测试、烧录、提交、推送、创建 PR、强推或重写远端历史。
- 修改已有硬件引脚、电源、网络凭据或持久化数据前，先读取现有文档和实现，说明影响范围并取得明确授权。
- 完整硬件操作前确认工程配置、开发板、串口和测试数据安全；构建或静态检查不能替代实板验证。

### Never

- 提交、复制他人路径或凭据、密钥或未验证的硬件结论；构建产物、下载依赖和工具缓存也不得提交。
- 混用系统 Python、Conda Python、裸 CMake 或其他 ESP-IDF 环境到同一 `build/`；无法确认 SDK 路径时停止并报告。
- 执行 `erase-flash`，覆盖、回退或混入他人的修改，或删除、放宽上传/下载/删除测试断言。
- 直接向 `main` 提交或推送。
