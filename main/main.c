#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH";

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
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "i2c bus recovery done");
}

void app_main(void) {
    ESP_LOGI(TAG, "boot: i2c recovery first");
    i2c_bus_recovery();

    ESP_LOGI(TAG, "backlight gpio23: 1kHz, 80%%");
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = 23,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_2,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 819,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));

    unsigned p0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "psram free before display: %u KB", (unsigned)(p0 / 1024));
    bsp_display_start();
    unsigned p1 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "psram free after display: %u KB (delta %d KB)",
             (unsigned)(p1 / 1024), (int)((p0 - p1) / 1024));

    lv_display_t *dd = lv_display_get_default();
    if (dd) {
        ESP_LOGI(TAG, "lvgl disp res: %dx%d", (int)
                 (int)lv_display_get_horizontal_resolution(dd),
                 (int)lv_display_get_vertical_resolution(dd));
    } else {
        ESP_LOGE(TAG, "lvgl default display is NULL!");
    }

    if (bsp_display_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_set_size(box, 400, 90);
        lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x00ff00), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_t *tt = lv_label_create(box);
        lv_label_set_text(tt, "DASH RENDER OK");
        lv_obj_set_style_text_color(tt, lv_color_black(), 0);
        lv_obj_center(tt);

        lv_obj_t *arc1 = lv_arc_create(scr);
        lv_obj_set_size(arc1, 380, 380);
        lv_obj_align(arc1, LV_ALIGN_LEFT_MID, 40, 30);
        lv_arc_set_rotation(arc1, 135);
        lv_arc_set_range(arc1, 0, 240);
        lv_arc_set_value(arc1, 120);
        lv_obj_set_style_arc_width(arc1, 26, LV_PART_MAIN | LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc1, lv_color_hex(0x222222), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc1, lv_color_hex(0x00c8ff), LV_PART_INDICATOR);
        lv_obj_clear_flag(arc1, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc1, NULL, LV_PART_KNOB);
        lv_obj_t *label1 = lv_label_create(scr);
        lv_label_set_text(label1, "120 km/h");
        lv_obj_set_style_text_color(label1, lv_color_white(), 0);
        lv_obj_align_to(label1, arc1, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *arc2 = lv_arc_create(scr);
        lv_obj_set_size(arc2, 380, 380);
        lv_obj_align(arc2, LV_ALIGN_RIGHT_MID, -40, 30);
        lv_arc_set_rotation(arc2, 135);
        lv_arc_set_range(arc2, 0, 80);
        lv_arc_set_value(arc2, 40);
        lv_obj_set_style_arc_width(arc2, 26, LV_PART_MAIN | LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc2, lv_color_hex(0x222222), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc2, lv_color_hex(0xff3b30), LV_PART_INDICATOR);
        lv_obj_clear_flag(arc2, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc2, NULL, LV_PART_KNOB);
        lv_obj_t *label2 = lv_label_create(scr);
        lv_label_set_text(label2, "40 x100 rpm");
        lv_obj_set_style_text_color(label2, lv_color_white(), 0);
        lv_obj_align_to(label2, arc2, LV_ALIGN_CENTER, 0, 0);

        bsp_display_unlock();
        ESP_LOGI(TAG, "ui ready");
    } else {
        ESP_LOGE(TAG, "bsp_display_lock failed");
    }

    int n = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        n++;
        if (n % 10 == 0) {
            ESP_LOGI(TAG, "alive %d sec", n);
        }
    }
}
