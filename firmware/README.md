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

## 本地 OTA 布局与首次迁移

应用使用 8 MB Flash 配置、`0x8000` 分区表偏移和双应用槽：

|分区|偏移|大小|
|---|---|---|
|nvs|0x9000|0x6000|
|phy_init|0xf000|0x1000|
|otadata|0x10000|0x2000|
|ota_0|0x20000|0x300000|
|ota_1|0x320000|0x300000|

启用 bootloader 首启回退；应用镜像上限为 3 MiB。未传入 `PROJECT_VER` 时，
本地版本为 `local-<sha12>`，无 Git 信息时为 `local`。可通过
`idf.py -DPROJECT_VER=ota-check-a build` 指定验收版本。

旧 factory 布局首次安装须经授权执行串口迁移。先按仓库开发环境说明激活 ESP-IDF，
在 `firmware/` 下使用同一 SDK 的 Python：

```sh
"$IDF_PYTHON" scripts/migrate_ota.py --port "$PORT" --backup-dir "$BACKUP_DIR" --build-dir build
```

`PORT` 为已核对的 ESP32-S3 串口，`BACKUP_DIR` 为仓库外的新备份目录。
工具要求旧表仍为 nvs/phy_init/factory 的已知布局，核对芯片、Flash、PSRAM、构建配置、
新表和每份镜像的扇区擦写范围；条件不符即退出。先保存旧 `[0,0x1d0000)` 完整区域、
分区表、NVS、PHY 和摘要，再按构建生成的分散镜像参数写入。
备份可能包含设备凭据，应仅保存在本机私有目录；工具不覆盖已有备份。
写入后在 ROM 下载模式比对 NVS/PHY 原始字节，全部一致才报告通过；按 EN 后启动，
再运行 `"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" -p "$PORT" monitor`。
使用 USB 供电且不接电池。不要用连续合并镜像进行保留 NVS 的迁移。
网页应用 OTA 仅更新备用应用槽与 otadata，不能迁移旧分区表或更新 bootloader。

## 当前运行入口

`main/top_main.c` 根据工程的 [`sdkconfig`](sdkconfig) 启动任务：

- 当前关闭彩条测试，运行 LVGL 圆弧、电源状态动画及 VBUS/SYS 电压条；开启彩条后改为屏幕/PSRAM 测试，两条显示路径互斥。
- 当前开启基础电源测试、调试关闭 BQ I²C 看门狗及 VBUS/SYS ADC，关闭软件故障注入；电源任务与显示任务独立运行。
- PSRAM 已启用启动初始化和内存测试，LVGL 动态内存使用 PSRAM；彩条路径中的循环 PSRAM 检查不在当前 LVGL 路径运行。
- 应用启动唯一 Wi-Fi service，复用持久化 profile、内建选网和重连；音频、触控及产品自检页面仍待接入。

## Wi-Fi 生命周期

`app/byai_network.c` 持有唯一网络服务，在 `byai_wifi` namespace 的 `prof_p`/`prof_b`
保存最多八份 profile。NVS 初始化错误直接返回，不自动擦除。本机存在私有
`main/app/wifi.local.h` 时，按配置 SSID 查询 profile：不存在则添加并连接；已存在时
保留其 priority，仅在密码不同或它不是 last-working profile 时更新/启用并直接连接。
只修改目标 SSID，其他 profile 保留。私有文件不提交；未配置本地目标时复用已存 profile，
没有 profile 时记录 `Wi-Fi unconfigured` 并保持离线。首次实板运行须授权使用凭据并写入
设备 NVS。本入口不启动 SoftAP、蓝牙、串口 CLI 或 MCP 配网。

GOT_IP 才发布网络就绪并记录 IP；断开或丢失 IP 时撤销就绪状态。
网络初始化成功不要求路由器或互联网可达。应用源文件由 `main/CMakeLists.txt`
显式列出，既有本地 `wifi_test.*` 不参与编译。上游组件许可见 [LICENSE](./LICENSE)。

## 局域网网页 OTA

从串口日志取得设备 IP 后，打开 `http://设备IP/ota`。页面内嵌于应用镜像，
无需 CDN；选择带附加 SHA-256 的 BYAI ESP32-S3 应用 `.bin`，不要选择合并镜像。
允许主动安装旧版或同版本。页面分别显示浏览器传输进度与设备接收/写入进度；
传输完成后仍须等待 SDK 校验、重启及首启确认。

|接口|行为|
|---|---|
|`GET /ota`|本地升级页面|
|`GET /api/ota/status`|本次启动标识、版本和 ELF 摘要、运行/备用分区、容量、确认状态、进度及错误|
|`POST /api/ota/upload`|`application/octet-stream` 原始应用镜像，必须有 Content-Length；异步接收，状态查询使用另一连接|

启动未确认或缺少可用双槽时拒绝写入。接收、校验和等待重启期间的第二份上传返回
409；非法类型/长度返回 400，超过槽容量返回 413，镜像校验失败返回 422，
接收/写入失败返回 500。接收连续 30 秒无进展则终止；未成功完成校验不选择新启动槽。
失败请求结束后可以重新上传。成功提交启动槽后，由同一 worker 延时两秒重启。

页面在会话存储中保存目标版本、目标 ELF 摘要与旧启动标识。响应丢失后继续查询；
只有启动标识改变、首启确认成功且运行摘要匹配目标才报告成功。
恢复旧摘要时显示“升级未生效/已回退”和实际版本；60 秒未确认则显示“结果待确认”，
保留查询，不把断网、传输 100% 或 HTTP 200 当作升级成功。

首启确认检查 NVS/网络栈、OTA worker 和全部 HTTP 路由的本地初始化结果。
不等待路由器、互联网、显示或音频外设；这些检查不代表应用业务逻辑已通过验证。
待确认应用初始化失败时请求 SDK 回退；无法回退或无法确认时不接受新 OTA。
OTA 不保存 NVS 升级历史，不更新分区表、bootloader、eFuse 或安全启动配置。

构建验收样本时，在已激活的 `firmware/` 目录分别生成明确版本；B 使用独立目录：

```sh
"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" -DPROJECT_VER=ota-check-a build
"$IDF_PYTHON" "$IDF_PATH/tools/idf.py" -B build/ota-check-b -DPROJECT_VER=ota-check-b build
```

以生成的应用描述、分区二进制和 `flasher_args.json` 核验版本、芯片、附加 SHA、
槽容量及 NVS/PHY 擦写范围；不能只凭文件名认定版本或使用未重新配置的 A 镜像。

### 验收边界

主机替身仅验证事务控制与失败收尾；浏览器本机状态样本仅验证页面交互。
NO0001（ESP32-S3、ESP-IDF v6.1）已完成双槽迁移、NVS/PHY 原始字节比对、A→B→A、
同镜像重装、NVS sentinel 保留及错误文件校验。107 次并发状态查询均成功，最大耗时约
0.70 秒；空/短文件返回 400，合并镜像、错误芯片/项目和缺少附加 SHA 的镜像返回 422，
均未进入备用槽写入。关闭连接前先发送 FIN，存在未读请求体时保留已排队的 HTTP 响应。

2026-11-15 完成剩余硬件场景：16 字节分块前缀上传从 `ota-check-c`/`ota_1` 切换到
`ota-check-a`/`ota_0`；合法头部后停止发送，30 秒无进展以 `ESP_ERR_TIMEOUT` 进入
`failed`，仅接收 288/1,469,520 字节，原启动 ID 与镜像不变，随后完整重传 `ota-check-c`
成功；丢弃上传响应后通过新 `boot_id`、运行摘要和 `boot_confirmed` 确认实际结果；
首启初始化失败的 `ota-check-bad` 自动回退到 `ota-check-a`/`ota_0`。验收结束时设备
运行已确认的 `ota-check-a`，状态为 `idle`、`network_ready=true`、回滚已启用。

A 阶段本地 OTA 硬件矩阵已完成。GitHub 发现、下载与发布属于后续 B 阶段，不包含在本次实现中。

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
