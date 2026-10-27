# BYAI 固件起步工程

当前固件以乐鑫 ADF 的 `esp_wifi_service` 例程为起点，目标芯片为 ESP32-S3。它提供 Wi-Fi 服务骨架，供后续逐步接入 BYAI 板上的其他模块。

## 基础功能

- 没有可用的 Wi-Fi 配置文件时，通过设备 SoftAP 启动 HTTP 网页配网；配网页面通过设备热点访问，不是已连接局域网内的管理后台。
- 在 NVS 中保存和管理 Wi-Fi 配置文件，并由 Wi-Fi service 选择网络、处理连接状态和相关事件。
- 通过 `wifi>` 串口交互入口管理配置文件、请求连接、启动或停止配网、重新评估网络，并可重启设备。
- 可按工程配置启用 BluFi 配网和 Wi-Fi MCP 工具；MCP 的 UART 传输也取决于相应配置。

当前代码仍是起步例程，音频、显示触控和电源等板级模块尚未接入，Wi-Fi 功能也尚未在 BYAI 实板上验证。例程源码的许可文本见 [LICENSE](./LICENSE)。

## 屏幕彩条测试

在 `menuconfig → BYAI hardware tests` 中打开 `Enable looping LCD TV color bars`
（`CONFIG_BYAI_LCD_COLOR_BARS_TEST`），重新编译、烧录后，屏幕每秒轮换一次
白、黄、青、绿、品红、红、蓝、黑八色竖条。Kconfig 默认关闭，当前工程的
`sdkconfig` 已开启；关闭后不再发送彩条画面。

当前硬件按固定背光亮度使用，尚未确认可用的背光调光接口，因此本测试不设置
50% 背光亮度；画面颜色测试与背光调光分开处理。
