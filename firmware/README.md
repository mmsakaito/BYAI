# BYAI 固件起步工程

当前固件以乐鑫 ADF 的 `esp_wifi_service` 例程为起点，目标芯片为 ESP32-S3，使用 ESP-IDF `v6.1`。现已接入 ST7789 显示、LVGL 界面及 BQ25890H 基础电源采集，作为产品功能的集成起点。

## CI 与构建产物

[`Firmware CI`](../.github/workflows/firmware-ci.yml) 在目标为 `dev`、`main` 的 PR
和这两个分支的推送中按变更路径触发，并行执行电源状态机主机测试与 ESP32-S3 固件构建。
仅修改 Markdown、`docs/`、Issue/PR 模板、许可证、Git 忽略规则、编辑器配置、根目录
`LOGO.png`、`ENV.local.example` 或安装配置 `config.toml` 时跳过整个工作流。
混合变更只要包含排除范围以外的文件就会运行；固件源码（含仅注释修改）、构建配置、
依赖锁文件、分区表、固件资源、子模块版本及工作流仍会触发。手动运行不受路径过滤限制。
固件使用固定 digest 的官方 ESP-IDF `v6.1` 容器，保留已跟踪的 `sdkconfig`、
依赖锁文件和 LVGL 子模块版本；构建改变配置或锁文件时直接失败。
容器启动后仅将本次 checkout 路径加入 Git 安全目录，并按容器内工作区路径定位 ccache。

- 所有运行均可恢复 ccache；仅 `dev`、`main` 推送在构建、打包和上传成功后保存缓存。
  PR 和手动运行不保存；缓存未命中时正常全量构建，旧缓存由 GitHub 自动淘汰。
- 成功的固件 job 上传保留 14 天的 Actions artifact：
  `BYAI_firmware_<sha>.bin`（应用镜像）、
  `BYAI_full_flash_<sha>.bin`（按本次 SDK 参数合并的 bootloader、分区表与应用镜像）、
  `BYAI_SHA256SUMS_<sha>.txt`（两个镜像的校验文件）；`<sha>` 为 checkout 提交的前 12 位。
- 下载并解压后，在同一目录运行 `sha256sum -c BYAI_SHA256SUMS_<sha>.txt` 校验。
  合并镜像覆盖从 bootloader 到应用末尾的连续地址区间，烧录前备份该区间内的 NVS 数据。

CI 使用仓库 checkout 和公开依赖，生成保留当前硬件调试配置的测试构建；
流程在产物上传和缓存保存后结束。实板验收按板卡、配置和实际现象记录到 PR 评论。
手动运行入口需 workflow 已进入默认分支；构建和下载权限由仓库 Actions 策略控制。

## 当前运行入口

`main/top_main.c` 根据工程的 [`sdkconfig`](sdkconfig) 启动任务：

- 当前关闭彩条测试，运行 LVGL 圆弧、电源状态动画及 VBUS/SYS 电压条；开启彩条后改为屏幕/PSRAM 测试，两条显示路径互斥。
- 当前开启基础电源测试、调试关闭 BQ I²C 看门狗及 VBUS/SYS ADC，关闭软件故障注入；电源任务与显示任务独立运行。
- PSRAM 已启用启动初始化和内存测试，LVGL 动态内存使用 PSRAM；彩条路径中的循环 PSRAM 检查不在当前 LVGL 路径运行。
- Wi-Fi 服务入口、音频、触控及产品自检页面为后续接入项。

## Wi-Fi 例程能力（当前未接入）

工程保留了上游 Wi-Fi 例程的组件依赖与配置，后续接入服务入口时可复用以下能力：

- 没有可用的 Wi-Fi 配置文件时，通过设备 SoftAP 启动 HTTP 网页配网；配网页面入口为设备热点上的 HTTP 服务。
- 在 NVS 中保存和管理 Wi-Fi 配置文件，并由 Wi-Fi service 选择网络、处理连接状态和相关事件。
- 通过 `wifi>` 串口交互入口管理配置文件、请求连接、启动或停止配网、重新评估网络，并可重启设备。
- 可按工程配置启用 BluFi 配网和 Wi-Fi MCP 工具；MCP 的 UART 传输也取决于相应配置。

当前应用源文件由 `main/CMakeLists.txt` 显式列出，Wi-Fi 服务入口待接入。例程源码的许可文本见 [LICENSE](./LICENSE)。

## 屏幕彩条测试

在 `menuconfig → BYAI hardware tests` 中打开 `Enable looping LCD TV color bars`
（`CONFIG_BYAI_LCD_COLOR_BARS_TEST`），重新编译、烧录后，屏幕每秒轮换一次
白、黄、青、绿、品红、红、蓝、黑八色竖条。Kconfig 默认关闭，当前工程的
`sdkconfig` 也已关闭；关闭时运行 LVGL 圆弧示例，不发送彩条画面。

当前硬件使用固定背光亮度，背光调光接口待核对；画面颜色与背光调光分别测试。

## BQ25890H 基础电源测试

在 `menuconfig → BYAI hardware tests` 中打开 `Enable BQ25890 basic power test`
（`CONFIG_BYAI_POWER_BASIC_TEST`，Kconfig 默认关闭，当前工程的 `sdkconfig` 已开启），
编译后按团队硬件操作流程烧录。
测试与显示任务独立运行，可以同时开启彩条。

原理图 U2 标注为 BQ25890RTWT，当前实板按 BQ25890H 使用。

- `20261001` 板卡使用 SCL GPIO38、SDA GPIO39，共享 I²C 总线；地址 `0x6A`，100 kHz。
- INT 接 GPIO4（成员提供的接线信息），当前不配置该引脚、不启用中断。
- 每轮读取芯片身份、输入源、充电状态、PG/VSYSMIN 及历史/当前故障，保留原始寄存器；轮末等待 1 秒，实际周期还包含 I²C、可选 ADC 及日志开销。
- 日志使用状态快照的 `current_valid`、`stale` 和 `error` 标识本轮有效性及错误；失败时保留最近完整成功数据并标记旧值。Q3 表示完整采集成功，供电、电池和充电功能分别按对应测试项验收。普通采样错误下轮重试，型号不符或 ADC 启动配置校验失败则停止测试。
- 基础模式读取芯片身份、供电/充电编码与故障状态；ADC 电压采集通过独立开关启用。电量、电流和电池检测为后续功能。
- 故障寄存器连续读取两次：第一次保存锁存故障，第二次保存当前故障；读取会消费锁存信息。
  watchdog 位记录 BQ25890H I²C 看门狗事件，按芯片手册解码。

电源调试的接线前提为 **USB 供电且不接电池**；运行前核对实际接线。

### 主机测试

不接硬件的电源状态机测试可在 Linux/macOS 使用 C 编译器运行：

```sh
bash tests/power_state/run.sh
```

该测试在主机运行，覆盖状态转换、旧值、超时和并发快照，产物位于临时目录。实板检查记录板卡、接线和实际采集结果。

驱动测试入口 `tests/power/run.sh` 的 ADC 路径依赖替身待补齐，补齐后运行该入口检查完整驱动。

### 可选 VBUS/SYS ADC 调试

在同一菜单依次开启 `CONFIG_BYAI_POWER_DEBUG_DISABLE_WATCHDOG` 和
`CONFIG_BYAI_POWER_ADC_TEST`（Kconfig 均默认关闭，当前工程的 `sdkconfig` 均已开启）。仅用于 USB 供电、不接电池的初期调试。
初始化仅清除 REG07 的 I²C 看门狗位并回读确认，保留充电安全计时器等其他位；
然后在独占 ADC 的前提下停止 ESP32 复位遗留的连续转换并回读确认，不改变输入检测配置；
若 DPDM 正在执行或初始化失败则停止测试。随后每轮开启约 1.2 秒连续转换窗口，读取 VBUS/SYS 并输出毫伏值，
再关闭连续转换并回读确认。每次采样前确认看门狗仍关闭，不逐次喂狗；
输入无效、模式回读不符、通信或收尾失败时不输出有效电压。
完整轮询周期包括约 1.2 秒转换等待、1 秒任务间隔及 I²C/日志开销。

关闭 I²C 看门狗后，ESP32 失联不再触发芯片超时恢复默认配置。
关闭测试选项、释放设备或 ESP32 重启不会主动重新开启它；需要显式配置或 BQ25890
自身上电复位。正式运行的前提是实现充电参数配置和定期喂狗策略。

### 电源状态快照

`power_state_get_snapshot()` 提供 Q1 未启用、Q2 初始化、Q3 正常、Q4 重试中、Q5 不可用状态，
以及最近完整成功的数据、错误和单调时间；查询不访问 I²C。前两次失败保留旧值并标记重试，
连续三次失败或 10 秒无结果进入不可用，一次完整成功后恢复正常。
严重初始化错误停止任务。ADC 关闭时只提供基础状态，电压明确标记未启用。

使用数据前检查 `current_valid`；Q4 的旧值标为重试中，Q5 的历史数据用于诊断。
状态图、状态职责表和接口说明见 [电源状态接口](../docs/power-state.md)。

当前界面保留左上角旋转圆弧，通过 `gui_create_power_panel()` 每 500 ms 读取快照，
显示英文 Q 状态、有效性及 VBUS/SYS 毫伏值（[#59](https://github.com/mmsakaito/BYAI/issues/59)）。
Q1～Q5 切换时，状态文字以 240 ms 淡入并上移；同一状态持续显示时保持位置。
两条电压条分别使用 3.9～5.5 V、0～4.844 V 标尺，当前有效电压以 240 ms 平滑过渡，
数字直接显示本次快照值。Q4 的旧值标为 `STALE`，条形和数值变为橙色；
Q5 清空电压指示并显示 `unavailable`。首次无数据、初始化或 ADC 关闭时，
数值显示 `--`、条形保留空轨道；ADC 关闭另标为 `ADC off`。
动画与电压条只更新界面，ADC 分辨率及采集周期沿用采集模块配置。
VBUS 下限采用 USB 供电且不接电池时的供电链路估算值 3.9 V，依据 BQ25890H 的推荐输入下限；
整板欠压复位对应的 VBUS 实测值通过后续降压测试标定。标尺在端点截断，数字保留实际快照值；
芯片欠压检测档位仍使用 `sdkconfig` 的现有配置。
FPS/CPU 性能监视器位于右上角，位置由 `lv_conf.h` 的 `LV_USE_PERF_MONITOR_POS` 配置。

可选 `CONFIG_BYAI_POWER_STATE_FAULT_TEST`（默认关闭）会以软件注入任务替代正常电源采集，
自动验证 Q4/Q5、恢复和真实 10 秒超时。此时采样值来自软件注入，正常采集任务保持关闭；
日志以 `synthetic=1` 标记合成数据，最终以 `SUITE result=PASS` 表示套件通过。
诊断任务结束后不会自动启动正常采集，需关闭选项并重新烧录正常版本。
