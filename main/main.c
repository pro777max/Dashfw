#include <stdlib.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "DASH";
static lv_obj_t *spd_arc, *rpm_arc, *spd_lbl, *rpm_lbl;
static int spd = 0, rpm = 800;

static void gauge_setup(lv_obj_t *arc, lv_align_t align, int32_t dx, int max) {
    lv_arc_set_range(arc, 0, max);
    lv_arc_set_value(arc, 0);
    lv_obj_set_size(arc, 320, 320);
    lv_obj_align(arc, align, dx, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

static void ui_timer_cb(lv_timer_t *t) {
    spd += (rand() % 9) - 4;
    if (spd < 0) spd = 0;
    if (spd > 220) spd = 220;
    rpm += (rand() % 501) - 250;
    if (rpm < 800) rpm = 800;
    if (rpm > 7500) rpm = 7500;
    bsp_display_lock(0);
    lv_arc_set_value(spd_arc, spd);
    lv_arc_set_value(rpm_arc, rpm / 100);
    lv_label_set_text_fmt(spd_lbl, "%d", spd);
    lv_label_set_text_fmt(rpm_lbl, "%d", rpm);
    bsp_display_unlock();
}

void app_main(void) {
    bsp_display_start();
    esp_err_t bl = bsp_display_backlight_on();
    ESP_LOGI(TAG, "backlight_on: %s", esp_err_to_name(bl));
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    spd_arc = lv_arc_create(lv_screen_active());
    gauge_setup(spd_arc, LV_ALIGN_LEFT_MID, 60, 220);
    rpm_arc = lv_arc_create(lv_screen_active());
    gauge_setup(rpm_arc, LV_ALIGN_RIGHT_MID, -60, 75);
    spd_lbl = lv_label_create(lv_screen_active());
    lv_obj_align_to(spd_lbl, spd_arc, LV_ALIGN_CENTER, 0, 0);
    rpm_lbl = lv_label_create(lv_screen_active());
    lv_obj_align_to(rpm_lbl, rpm_arc, LV_ALIGN_CENTER, 0, 0);
    lv_timer_create(ui_timer_cb, 100, NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "ui ready");
}
