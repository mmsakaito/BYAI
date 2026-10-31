#pragma once

/**
 * @brief 可开关的 BQ25890 轮询任务，参数未使用。
 * @note 独占本驱动设备句柄，借用板级共享总线。瞬态通信失败每秒重试，
 *       型号不符释放设备并退出；不驱动 INT、不写充电参数或持久化数据。
 */
void power_test_task(void *arg);
