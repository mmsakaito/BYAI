# BYAI 固件起步工程

当前固件以乐鑫 ADF 的 `esp_wifi_service` 例程为起点，目标芯片为 ESP32-S3。它提供 Wi-Fi 服务骨架，供后续逐步接入 BYAI 板上的其他模块。

## 基础功能

- 没有可用的 Wi-Fi 配置文件时，通过设备 SoftAP 启动 HTTP 网页配网；配网页面通过设备热点访问，不是已连接局域网内的管理后台。
- 在 NVS 中保存和管理 Wi-Fi 配置文件，并由 Wi-Fi service 选择网络、处理连接状态和相关事件。
- 通过 `wifi>` 串口交互入口管理配置文件、请求连接、启动或停止配网、重新评估网络，并可重启设备。
- 可按工程配置启用 BluFi 配网和 Wi-Fi MCP 工具；MCP 的 UART 传输也取决于相应配置。

以上是起步例程的 Wi-Fi 服务能力；当前主程序运行屏幕/PSRAM 测试和可选的电源通信测试，
尚未接入这些 Wi-Fi 服务入口，音频和触控仍待接入。例程源码的许可文本见 [LICENSE](./LICENSE)。

## 屏幕彩条测试

在 `menuconfig → BYAI hardware tests` 中打开 `Enable looping LCD TV color bars`
（`CONFIG_BYAI_LCD_COLOR_BARS_TEST`），重新编译、烧录后，屏幕每秒轮换一次
白、黄、青、绿、品红、红、蓝、黑八色竖条。Kconfig 默认关闭，当前工程的
`sdkconfig` 已开启；关闭后不再发送彩条画面。

当前硬件按固定背光亮度使用，尚未确认可用的背光调光接口，因此本测试不设置
50% 背光亮度；画面颜色测试与背光调光分开处理。

## BQ25890RTWT 基础电源测试

在 `menuconfig → BYAI hardware tests` 中打开 `Enable BQ25890 basic power test`
（`CONFIG_BYAI_POWER_BASIC_TEST`，默认关闭），编译后按团队硬件操作流程烧录。
测试与显示任务独立运行，可以同时开启彩条。

- `20261001` 板卡使用 SCL GPIO38、SDA GPIO39，共享 I²C 总线；地址 `0x6A`，100 kHz。
- INT 接 GPIO4（成员提供的接线信息），当前不配置该引脚、不启用中断。
- 每秒读取芯片身份、输入源、充电状态、PG/VSYSMIN 及历史/当前故障，保留原始寄存器。
- `valid=1` 只表示本轮读取成功；不表示供电、电池或充电功能已经验收。
  `valid=0` 会附错误及累计计数，不返回旧状态；瞬态错误下轮重试，型号不符则停止测试。
- 不写充电配置，不启用 ADC，不估算电量、电流，也不根据寄存器状态判断电池是否连接。
- 故障寄存器连续读取两次：第一次保存锁存故障，第二次保存当前故障；读取会消费锁存信息。
  默认模式可能报告 watchdog 位，需按芯片手册解释，不能直接等同于 ESP32 看门狗或供电故障。

首轮实板条件为 **USB 供电且不接电池**；软件不能核实实际接线。
测量与运行步骤、证据和未验证项见 [基础电源验证记录](../docs/power-basic-validation.md)。

不接硬件的驱动测试可在 Linux/macOS 使用 C 编译器运行：

```sh
bash tests/power/run.sh
```

该测试模拟 I²C 响应与错误，产物位于临时目录；不能替代实板通信和电压测量。
