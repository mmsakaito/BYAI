#pragma once

/**
 * @brief 默认关闭的软件注入套件；只在诊断配置下代替正常电源采集任务运行一次。
 *
 * @param arg 未使用，应传 NULL。
 *
 * @note 创建前必须调用 power_state_begin()。独占状态写入，不创建 I2C 设备，
 *       不写芯片寄存器；所有电压均为合成数据，不能用于硬件测量结论。
 *       超时观察者仅查询快照，通知本任务后自行退出。套件结束后本任务退出，
 *       不自动恢复正常采集；关闭配置并烧录正常固件后才能恢复采集。
 */
void power_state_fault_test_task(void *arg);
