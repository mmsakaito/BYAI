/**
 * @file krnl_lvgl.c
 * @brief LVGL 的 PSRAM 分配适配、同步 SPI 刷新及电源状态界面。
 *
 * @details 主任务串行执行刷新、动画及电源标签定时器；电源采集任务发布快照。
 *          显示对象及缓冲随常驻任务保留至重启，LVGL 操作集中在主任务中。
 */
#include <stddef.h>
#include "krnl_lvgl.h"

#include <string.h>

#include "esp_log.h"
#include "gui.h"
#include "kernel.h"
#include "lvgl.h"
#include "sysdef.h"
#include "st7789.h"

lv_display_t *display;

/**
 * @brief 为 LVGL 分配 PSRAM；内存由 ESP-IDF 堆管理。
 * @param size 请求的字节数。
 * @return 分配的指针，失败返回 NULL；调用方通过 lv_free_core 释放。
 */
void* lv_malloc_core(size_t size_t) {
	return heap_caps_malloc(size_t, MALLOC_CAP_SPIRAM);
}
/**
 * @brief 调整 LVGL 的 PSRAM 分配。
 * @param ptr 原分配指针，可为 NULL；成功后不得再使用原指针。
 * @param size 新的字节数，语义沿用 heap_caps_realloc。
 * @return 调整后的指针；非零大小分配失败返回 NULL，原分配仍归调用方。
 */
void* lv_realloc_core(void* ptr, size_t size)
{
	return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

/**
 * @brief 将 LVGL 分配归还 ESP-IDF 堆。
 * @param ptr lv_malloc_core 或 lv_realloc_core 返回的指针，可为 NULL。
 */
void lv_free_core(void* ptr)
{
	return heap_caps_free(ptr);
}
/* LVGL 分配直接使用 ESP-IDF 堆，此初始化入口为空。 */
void  lv_mem_init(void){}

/**
 * @brief 同步发送刷新区像素，完成后通知 LVGL 可复用缓冲。
 * @param disp_drv LVGL 传入的显示对象；当前完成通知仍使用全局 display。
 * @param area 含端点的刷新矩形，调用期间借用，不保存。
 * @param px_map 借用的 RGB565 像素缓冲；同步 SPI 传输结束后才通知 LVGL 复用。
 * @note LVGL 任务独占显示设备，串行调用同步 SPI，像素按原 RGB565 字节序发送。
 *       每块最多 4000 字节；分块循环保持同一 px_map 和地址窗口，
 *       因而多块刷新会重复发送首段。注册的 320×5×2 字节缓冲落在单块内。
 *       SPI 返回值被忽略，传输报错后仍通过全局 display 通知刷新完成。
 */
void cb_flush(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map) {
	uint32_t cnt=(area->y2-area->y1+1)*(area->x2-area->x1+1)*2;
	while (cnt>0) {
		uint32_t cnt_max;
		if (cnt>=4000)
			cnt_max=4000;
		else
			cnt_max=cnt;
		spi_transaction_t t={
			.length=cnt_max*8,
			.tx_buffer = px_map,
		};
		LCD_Address_Set(area->x1,area->y1,area->x2,area->y2);
		spi_device_polling_transmit(hspi2,&t);
		cnt-=cnt_max;
	}

	lv_display_flush_ready(display);
}
/**
 * @brief LVGL 动画回调；在 LVGL 任务中更新圆弧末端角度。
 * @param obj 借用的常驻圆弧对象，动画不接管对象所有权。
 * @param value 动画插值得到的角度，单位为度。
 */
void arc_angle_anim(void *obj, int32_t value)
{
	lv_arc_set_end_angle((lv_obj_t *)obj, (uint16_t)value);
}
void main_lvgl(void *p) {
	lv_init();
	LV_ATTRIBUTE_MEM_ALIGN
	/* 每块分配 20 行，分配结果直接用于注册；向 LVGL 注册的容量为 5 行。 */
	uint8_t *buffer1 = heap_caps_malloc(DISP_HOR * 20 * 2,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT);
	uint8_t *buffer2 = heap_caps_malloc(DISP_HOR * 20 * 2,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT);
	display = lv_display_create(DISP_HOR, DISP_VER);
	lv_display_set_flush_cb(display,cb_flush);
	lv_display_set_buffers(display, buffer1,buffer2,DISP_HOR * 5 * 2,LV_DISPLAY_RENDER_MODE_PARTIAL);

	/* 小圆弧与状态文字共用顶部区域，电压条占用中下部，性能浮窗位于右上角。 */
	static lv_obj_t *arc;
	arc = lv_arc_create(lv_scr_act());
	lv_obj_set_size(arc, 48, 48);
	lv_obj_set_pos(arc, 24, 24);

	lv_obj_remove_style(arc, NULL, LV_PART_KNOB);           /* 隐藏旋钮 */
	lv_arc_set_bg_angles(arc, 0, 360);                      /* 背景整圈 */
	lv_arc_set_start_angle(arc, 0);
	lv_arc_set_end_angle(arc, 120);                         /* 初始弧长 120° */

	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, arc);
	lv_anim_set_exec_cb(&a, arc_angle_anim);
	lv_anim_set_values(&a, 120, 480);                       /* 让弧头转起来 */
	lv_anim_set_time(&a, 1000);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_start(&a);
	gui_create_power_panel();
	/* handler 串行执行定时器与动画；每轮 tick 增加 5 ms，绘制耗时另占实际时间。 */
	while (1) {
		lv_task_handler();
		lv_tick_inc(5);
		vTaskDelay(pdMS_TO_TICKS(5));
	}
}