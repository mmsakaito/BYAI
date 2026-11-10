/**
 * @file power_state_fault_test.h
 * @brief 诊断配置下替代正常采集的软件故障注入任务接口。
 */

#pragma once

/**
 * @brief 默认关闭的软件注入套件；只在诊断配置下代替正常电源采集任务运行一次。
 *
 * @param arg 未使用，应传 NULL。
 *
 * @note 创建前必须调用 power_state_begin()。独占状态写入，电压字段来自软件合成标记值；
 *       测试通过状态接口注入结果，BQ25890 设备和寄存器保持原状。
 *       超时观察者查询快照，通知本任务后自行退出；套件结束后注入任务退出。
 *       恢复正常采集须关闭注入配置并烧录正常固件。
 */
void power_state_fault_test_task(void *arg);
