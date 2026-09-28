#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH";
static lv_obj_t *arc1, *arc2, *label1, *label2;

static void i2c_bus_recovery(void) {
    const int pins[] = {8, 22};
    for (int p = 0; p < 2; p++) {
        gpio_set_pull_mode((gpio_num_t)pins[p], GPIO_PULLUP_ONLY);
        gpio_set_direction((gpio_num_t)pins[p], GPIO_MODE_OUTPUT_OD);
        for (int i = 0; i < 9; i++) {
            gpio_set_level((gpio_num_t)pins[p], 0);
            vTaskDelay(pdMS_TO_TICKS(5));
            gpio_set_level((gpio_num_t)pins[p], 1);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        gpio_set_direction((gpio_num_t)pins[p], GPIO_MODE_INPUT);
    }
    ESP_LOGI(TAG, "i2c bus recovery done");
}

void app_main(void) {
    ESP_LOGI(TAG, "boot: i2c recovery first");
    i2c_bus_recovery();

    ESP_LOGI(TAG, "backlight on GPIO23");
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 23,
        .duty = 1023,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));

    ESP_LOGI(TAG, "display start");
    bsp_display_start();
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    arc1 = lv_arc_create(scr);
    lv_obj_set_size(arc1, 380, 380);
    lv_obj_align(arc1, LV_ALIGN_LEFT_MID, 40, 0);
    lv_arc_set_rotation(arc1, 135);
    lv_arc_set_range(arc1, 0, 240);
    lv_arc_set_value(arc1, 0);
    lv_obj_set_style_arc_width(arc1, 26, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc1, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc1, lv_color_hex(0x00c8ff), LV_PART_INDICATOR);
    lv_obj_remove_flag(arc1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc1, NULL, LV_PART_KNOB);
    label1 = lv_label_create(scr);
    lv_label_set_text(label1, "0 km/h");
    lv_obj_set_style_text_color(label1, lv_color_white(), 0);
    lv_obj_align_to(label1, arc1, LV_ALIGN_CENTER, 0, 0);

    arc2 = lv_arc_create(scr);
    lv_obj_set_size(arc2, 380, 380);
    lv_obj_align(arc2, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_arc_set_rotation(arc2, 135);
    lv_arc_set_range(arc2, 0, 80);
    lv_arc_set_value(arc2, 0);
    lv_obj_set_style_arc_width(arc2, 26, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc2, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc2, lv_color_hex(0xff3b30), LV_PART_INDICATOR);
    lv_obj_remove_flag(arc2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc2, NULL, LV_PART_KNOB);
    label2 = lv_label_create(scr);
    lv_label_set_text(label2, "0 x100 rpm");
    lv_obj_set_style_text_color(label2, lv_color_white(), 0);
    lv_obj_align_to(label2, arc2, LV_ALIGN_CENTER, 0, 0);
    bsp_display_unlock();
    ESP_LOGI(TAG, "ui ready");

    int spd = 0, rpm = 8, dir = 1;
    while (1) {
        spd += dir * 4;
        rpm += dir * 1;
        if (spd >= 240) dir = -1;
        if (spd <= 0) dir = 1;
        if (rpm > 80) rpm = 80;
        if (rpm < 8) rpm = 8;
        if (bsp_display_lock(0)) {
            lv_arc_set_value(arc1, spd);
            lv_arc_set_value(arc2, rpm);
            lv_label_set_text_fmt(label1, "%d km/h", spd);
            lv_label_set_text_fmt(label2, "%d x100 rpm", rpm);
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}
