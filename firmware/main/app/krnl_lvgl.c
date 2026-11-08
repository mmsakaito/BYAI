#include <stddef.h>
#include "krnl_lvgl.h"

#include <string.h>

#include "esp_log.h"
#include "kernel.h"
#include "lvgl.h"
#include "sysdef.h"
#include "st7789.h"

lv_display_t *display;

void* lv_malloc_core(size_t size_t) {
	return heap_caps_malloc(size_t, MALLOC_CAP_SPIRAM);
}
void* lv_realloc_core(void* ptr, size_t size)
{
	return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

void lv_free_core(void* ptr)
{
	return heap_caps_free(ptr);
}
void  lv_mem_init(void){}

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
void arc_angle_anim(void *obj, int32_t value)
{
	lv_arc_set_end_angle((lv_obj_t *)obj, (uint16_t)value);
}
void main_lvgl(void *p) {
	lv_init();
	LV_ATTRIBUTE_MEM_ALIGN
	uint8_t *buffer1 = heap_caps_malloc(DISP_HOR * 20 * 2,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT);
	uint8_t *buffer2 = heap_caps_malloc(DISP_HOR * 20 * 2,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT);
	display = lv_display_create(DISP_HOR, DISP_VER);
	lv_display_set_flush_cb(display,cb_flush);
	lv_display_set_buffers(display, buffer1,buffer2,DISP_HOR * 5 * 2,LV_DISPLAY_RENDER_MODE_PARTIAL);

	/*test*/
	static lv_obj_t *screen;
	screen = lv_obj_create(NULL);
	static lv_obj_t *arc;
	arc = lv_arc_create(lv_scr_act());
	lv_obj_set_size(arc, 80, 80);
	lv_obj_center(arc);

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
	while (1) {
		lv_task_handler();
		lv_tick_inc(5);
		vTaskDelay(pdMS_TO_TICKS(5));
	}
}