# BYAI Repository Guide

## 项目概况

- BYAI 是基于 ESP32-S3 的 AI 语音交互终端项目。
- 当前仓库仍是文档与规划基线，固件源码、构建系统、自动化测试和硬件资源尚未提交。
- 面向成员的开发环境安装入口见 [`docs/development-environment.md`](docs/development-environment.md)。
- 使用 ESP-IDF 官方文档时，优先参考乐鑫文档；需要检索官方资料时可使用 ESP 文档 MCP。

## 开发环境

- 成员按 [`docs/development-environment.md`](docs/development-environment.md) 安装；AI 执行构建或检查时使用下方的本机环境配置和平台命令。

### 本机环境配置

- 复制 [`ENV.local.example`](ENV.local.example) 为 `ENV.local`，填写本机实际的 `ESP_IDF_PATH`、`IDF_TOOLS_PATH` 和 `IDF_PYTHON_ENV_PATH`；`ENV.local` 已忽略，不提交到 Git。
- 三个路径必须来自同一套 ESP-IDF/EIM 安装：`ESP_IDF_PATH` 包含 `tools/idf.py`，`IDF_TOOLS_PATH` 包含 EIM 工具和激活脚本，`IDF_PYTHON_ENV_PATH` 指向该安装提供的 Python 环境。
- EIM 或 ESP-IDF 更新后重新核对这三个路径，不要填写尚不存在的预期路径，也不要混用系统 Python、Conda Python 或其他 SDK 的路径。
- Linux/macOS/WSL 可在仓库根目录运行 `set -a; . ./ENV.local; set +a` 导入变量，然后使用 `IDF_TOOLS_PATH` 下对应版本的 EIM 激活脚本；Windows 应按 EIM 生成的 PowerShell 激活脚本在当前会话中加载。

### 路径约定

- ESP-IDF 根目录（`IDF_PATH`）必须指向包含 `tools/idf.py` 的目录；不要只指向工具目录或项目目录。
- EIM 工具目录（`IDF_TOOLS_PATH`）和 Python 环境目录（`IDF_PYTHON_ENV_PATH`）必须来自同一套 EIM 安装，并通过激活脚本产生或确认，不要手工拼接另一套路径。
- EIM 的默认安装根目录为 Windows 的 `C:\\Espressif`，Linux/macOS 的 `$HOME/.espressif`；实际路径以 EIM 安装结果为准。
- 固件项目的构建输出默认放在项目根目录的 `build/`，由 ESP-IDF/CMake 生成；该目录属于构建产物，不提交到 Git。
- 烧录和监视使用当前项目选择的串口 `PORT`；在固件接入后，README 必须补充目标板、串口、构建目录和完整命令。

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

固件接入后约定应用根目录为 `firmware/`；当前仓库尚未创建该目录，以下命令保留为接入 ESP-IDF 后的标准操作。

### Unix-like systems (Linux/macOS/WSL)

- Activate (EIM): 在仓库根目录运行 `set -a; . ./ENV.local; set +a`，然后 source `"$IDF_TOOLS_PATH/activate_idf_<version>.sh"`；Fish 使用 `source "$IDF_TOOLS_PATH/activate_idf_<version>.fish"`。Linux 和 macOS 的 EIM 命名规范相同，不要改用 Windows 的 PowerShell profile 脚本。
- Activate (传统安装): 如果不是 EIM 安装，Unix-like 系统才使用 `. "$ESP_IDF_PATH/export.sh"`。不要直接执行激活脚本；必须使用 `source`/`.` 使环境变量留在当前 shell。
- Build: 进入 `firmware/`，运行 `IDF_PYTHON="$IDF_PYTHON_ENV_PATH/bin/python"`，然后 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" build`。
- Clean: Python 路径不一致时运行 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" fullclean` 后再构建。
- Target / flash: `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" set-target esp32s3`；使用 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" -p PORT flash monitor` 烧录并监视。

### Windows

- Activate (EIM, PowerShell): 从 `ENV.local` 读取并设置路径，使用 `IDF_TOOLS_PATH\Microsoft.<version>.PowerShell_profile.ps1`；例如 ESP-IDF 6.1 使用 `Microsoft.v6.1.PowerShell_profile.ps1`。必须在当前 PowerShell 中 dot-source：`. "$env:IDF_TOOLS_PATH\Microsoft.v6.1.PowerShell_profile.ps1"`。
- Activate (EIM, CMD): EIM 只有在配置 `create_bat_activation_script = true` 时才生成 Windows `.bat` 激活脚本；存在时使用 `call` 调用。没有该文件时使用 PowerShell，不要假设 EIM 一定提供 CMD 脚本。
- Activate (传统安装): 非 EIM 安装的 PowerShell/CMD 才使用 SDK 根目录下的 `export.ps1`/`export.bat`。
- Build (PowerShell): 进入 `firmware/`，运行 `$IDF_PYTHON = Join-Path $env:IDF_PYTHON_ENV_PATH "python.exe"`，然后 `& $IDF_PYTHON "$env:IDF_PATH/tools/idf.py" build`。
- Build (CMD): 进入 `firmware\`，运行 `set "IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\python.exe"`，然后 `"%IDF_PYTHON%" "%IDF_PATH%\tools\idf.py" build`。
- Clean / target / flash: 使用当前 shell 中对应的 `IDF_PYTHON`，分别运行 `fullclean`、`set-target esp32s3` 或 `-p PORT flash monitor`。

## 项目结构

- 仓库级说明放在 `README.md`，协作规则放在 `CONTRIBUTING.md`，需求和路线文档放在 `docs/`。
- 固件接入后，按板级配置、驱动、应用、测试和资源划分清晰的顶层目录，并同步更新 README。
- 构建产物、下载依赖、工具缓存和本机 IDE/环境设置不提交；`.vscode/` 和 `ENV.local` 已由 `.gitignore` 忽略。

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
