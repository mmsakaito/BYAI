/**
 * @file gui.c
 * @brief 通过电源快照更新状态切换动画及 VBUS/SYS 电压条。
 *
 * @details 所有对象、定时器和文字缓冲由 LVGL 任务串行使用；采集控制由电源任务负责。
 * @copyright Copyright (c) <2026> <BYAI Group>
 */

#include "gui.h"

#include <stdio.h>

#include "lvgl.h"
#include "power_state.h"

/* VBUS 显示范围按 USB 供电链路估算为 3.9～5.5 V；SYS 上限取 ADC 的 2304 + 127×20 mV。 */
enum {
    STATE_Y = 24,
    TRANSITION_MS = 240,
    VBUS_MIN_MV = 3900,
    VBUS_MAX_MV = 5500,
    SYS_MAX_MV = 4844,
};

/** @brief 单路电压控件及上次显示状态；缓存用于按变化重绘。 */
typedef struct {
    lv_obj_t *bar;
    lv_obj_t *value;
    char text[24]; /**< 数字标签借用的常驻缓冲，单位为毫伏。 */
    uint32_t color; /**< 当前有效值的条形颜色；旧值统一使用橙色。 */
    uint16_t last_mv; /**< 上次交给控件的快照读数，动画帧由 LVGL 单独推进。 */
    bool initialized;
    bool visible; /**< 完整电压可显示；为 false 时绘制空轨道和 --。 */
    bool stale; /**< 标识当前展示的是历史读数。 */
} voltage_widget_t;

/* LVGL 借用静态文字缓冲；对象及缓冲随常驻界面保留。 */
static struct {
    lv_obj_t *state;
    lv_obj_t *validity;
    const char *validity_text;
    voltage_widget_t vbus;
    voltage_widget_t sys;
    power_state_id_t last_state;
    bool initialized;
} panel;

/**
 * @brief 在 LVGL 动画帧中淡入并上移状态文字。
 * @param object 借用的状态标签。
 * @param opacity 从透明到不透明的动画进度，同时确定 8 像素的位移。
 */
static void animate_state(void *object, int32_t opacity)
{
    lv_obj_t *label = object;
    lv_obj_set_style_text_opa(label, opacity, 0);
    lv_obj_set_y(label, STATE_Y + (LV_OPA_COVER - opacity) * 8 / LV_OPA_COVER);
}

/**
 * @brief 更新状态名称及颜色；首次直接显示，此后在状态变化时播放入场动画。
 * @param state 本次快照的 Q 状态；未知枚举按 Q5 显示。
 * @note 动画由当前 LVGL 任务处理，重复状态保留当前动画进度。
 */
static void update_state(power_state_id_t state)
{
    const char *name;
    uint32_t color;
    switch (state) {
    case POWER_STATE_Q1_DISABLED:
        name = "Q1 Disabled";
        color = 0x64748b;
        break;
    case POWER_STATE_Q2_INITIALIZING:
        name = "Q2 Initializing";
        color = 0x2563eb;
        break;
    case POWER_STATE_Q3_READY:
        name = "Q3 Ready";
        color = 0x16a34a;
        break;
    case POWER_STATE_Q4_RETRYING:
        name = "Q4 Retrying";
        color = 0xd97706;
        break;
    case POWER_STATE_Q5_UNAVAILABLE:
    default:
        state = POWER_STATE_Q5_UNAVAILABLE;
        name = "Q5 Unavailable";
        color = 0xdc2626;
        break;
    }

    if (panel.initialized && panel.last_state == state) {
        return;
    }
    lv_label_set_text_static(panel.state, name);
    lv_obj_set_style_text_color(panel.state, lv_color_hex(color), 0);
    if (panel.initialized) {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, panel.state);
        lv_anim_set_exec_cb(&animation, animate_state);
        lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&animation, TRANSITION_MS);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_start(&animation);
    }
    panel.last_state = state;
    panel.initialized = true;
}

/**
 * @brief 按有效性更新电压条和数字，读数变化时平滑移动条形。
 * @param widget 常驻电压控件及其自有文字缓冲。
 * @param millivolts 快照提供的原始换算毫伏值。
 * @param visible 本次存在完整的当前或历史电压。
 * @param stale 历史值标记；旧值以橙色立即定位到历史读数。
 * @note 数字直接显示快照值；动画只改变条形位置。不可用时清空指示并显示 --。
 *       空轨道使用条形下限绘制，数字占位符标识电压不可用。
 */
static void update_voltage(voltage_widget_t *widget, uint16_t millivolts, bool visible, bool stale)
{
    bool mode_changed = !widget->initialized || widget->visible != visible || widget->stale != stale;
    /* 保持同一目标时继续已有动画，读数或显示模式变化时才重新设置控件。 */
    if (!mode_changed && (!visible || widget->last_mv == millivolts)) {
        return;
    }

    if (mode_changed) {
        lv_obj_set_style_bg_opa(widget->bar, visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(widget->bar,
                                 lv_color_hex(stale ? 0xd97706 : widget->color), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(widget->value,
                                   lv_color_hex(stale ? 0xd97706 : 0x334155), 0);
    }
    if (visible) {
        snprintf(widget->text, sizeof(widget->text), "%u mV", (unsigned)millivolts);
        lv_label_set_text_static(widget->value, widget->text);
        lv_bar_set_value(widget->bar, millivolts, stale ? LV_ANIM_OFF : LV_ANIM_ON);
    } else {
        lv_label_set_text_static(widget->value, "--");
        lv_bar_set_value(widget->bar, lv_bar_get_min_value(widget->bar), LV_ANIM_OFF);
    }
    widget->last_mv = millivolts;
    widget->visible = visible;
    widget->stale = stale;
    widget->initialized = true;
}

/**
 * @brief 读取一致性快照并刷新界面；Q4 展示旧值，Q5 清空电压条。
 * @param timer 常驻 LVGL 定时器，回调使用模块静态控件。
 * @note 查询只涉及内存和单调时钟；Q3 的完整有效样本与 Q4 的带标记历史样本可显示电压。
 *       ADC 关闭及首次无数据时显示占位符，查询失败按 Q5 展示。
 */
static void update_power_panel(lv_timer_t *timer)
{
    (void)timer;
    power_state_snapshot_t snapshot = { 0 };
    if (power_state_get_snapshot(&snapshot) != ESP_OK) {
        snapshot.state = POWER_STATE_Q5_UNAVAILABLE;
    }
    update_state(snapshot.state);

    /* 同一份快照决定状态、数字和条形，保持当前值及历史值标记一致。 */
    bool current = snapshot.state == POWER_STATE_Q3_READY &&
                   snapshot.has_data && snapshot.current_valid && !snapshot.is_stale;
    bool stale = snapshot.state == POWER_STATE_Q4_RETRYING &&
                 snapshot.has_data && snapshot.is_stale;
    bool visible = (current || stale) && snapshot.data.voltage_available;
    const char *validity;
    if (snapshot.state == POWER_STATE_Q5_UNAVAILABLE) {
        validity = "unavailable";
    } else if ((current || stale) && !snapshot.data.voltage_available) {
        validity = stale ? "STALE / ADC off" : "valid / ADC off";
    } else {
        validity = current ? "valid" : stale ? "STALE" : "no data";
    }
    if (panel.validity_text != validity) {
        lv_label_set_text_static(panel.validity, validity);
        panel.validity_text = validity;
    }
    update_voltage(&panel.vbus, snapshot.data.vbus_mv, visible, stale);
    update_voltage(&panel.sys, snapshot.data.sys_mv, visible, stale);
}

/**
 * @brief 创建单行电压文字、条形与量程标尺，控件归活动屏幕所有。
 * @param widget 保存常驻控件指针及文字缓冲。
 * @param name 电压名称，LVGL 借用静态字符串。
 * @param scale 标尺文字，LVGL 借用静态字符串。
 * @param y 该行标题相对屏幕顶端的位置。
 * @param minimum 条形下限，单位毫伏。
 * @param maximum 条形上限，单位毫伏。
 * @param color 当前有效电压的条形颜色。
 */
static void create_voltage_widget(voltage_widget_t *widget, const char *name, const char *scale,
                                  int32_t y, int32_t minimum, int32_t maximum, uint32_t color)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text_static(title, name);
    lv_obj_set_pos(title, 24, y);

    widget->value = lv_label_create(screen);
    lv_obj_set_width(widget->value, 176);
    lv_obj_set_style_text_align(widget->value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widget->value, 120, y);
    widget->color = color;

    widget->bar = lv_bar_create(screen);
    lv_obj_set_pos(widget->bar, 24, y + 24);
    lv_obj_set_size(widget->bar, 272, 12);
    lv_bar_set_range(widget->bar, minimum, maximum);
    lv_obj_set_style_anim_duration(widget->bar, TRANSITION_MS, 0);
    lv_obj_set_style_bg_color(widget->bar, lv_color_hex(0xe2e8f0), 0);

    lv_obj_t *caption = lv_label_create(screen);
    lv_label_set_text_static(caption, scale);
    lv_obj_set_width(caption, 272);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(0x64748b), 0);
    lv_obj_set_pos(caption, 24, y + 40);
}

void gui_create_power_panel(void)
{
    panel.state = lv_label_create(lv_screen_active());
    lv_obj_set_pos(panel.state, 92, STATE_Y);
    panel.validity = lv_label_create(lv_screen_active());
    lv_obj_set_pos(panel.validity, 92, 52);
    lv_obj_set_style_text_color(panel.validity, lv_color_hex(0x64748b), 0);
    /* 320×240 布局：顶部状态行，下面两行分别放置名称、数值、条形与量程。 */
    create_voltage_widget(&panel.vbus, "VBUS", "3.9 - 5.5 V", 96, VBUS_MIN_MV, VBUS_MAX_MV, 0x2563eb);
    create_voltage_widget(&panel.sys, "SYS", "0 - 4.844 V", 166, 0, SYS_MAX_MV, 0x16a34a);
    lv_timer_t *timer = lv_timer_create(update_power_panel, 500, NULL);
    update_power_panel(timer);
}
