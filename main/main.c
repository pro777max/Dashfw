#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH_FINAL";

static lv_obj_t *arc1 = NULL;
static lv_obj_t *arc2 = NULL;
static lv_obj_t *label1 = NULL;
static lv_obj_t *label2 = NULL;

static void force_backlight_gpio23(void) {
    ESP_LOGI(TAG, "Forcing backlight on GPIO 23...");
    
    // ?????????? ???, ????? ???????? ?? ????? ???????? BSP
    gpio_reset_pin(GPIO_NUM_23);
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER_1,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = 23,       // ??? ????????? ???!
        .duty           = 1023,     // 100% ???????
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    // ??????????: ????? ?????? %, ??????? ????? ??????????
    ESP_LOGI(TAG, "Backlight is ON on GPIO 23"); 
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Dashboard...");

    // 1. ?????????????? ??????? (BSP ???????? MIPI DSI ? ?????? EK79007)
    ESP_LOGI(TAG, "Init Display...");
    bsp_display_start();
    
    // 2. ????????????? ???????? ????????? ?? ?????????? ???? (GPIO 23)
    force_backlight_gpio23();

    bsp_display_lock(0);
    
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // 3. ?????? ?????????
    ESP_LOGI(TAG, "Creating UI...");
    
    // ???? 1 (?????????)
    arc1 = lv_arc_create(scr);
    lv_obj_set_size(arc1, 400, 400);
    lv_obj_align(arc1, LV_ALIGN_LEFT_MID, 50, 0);
    lv_arc_set_rotation(arc1, 135);
    lv_arc_set_range(arc1, 0, 240);
    lv_arc_set_value(arc1, 120);
    lv_obj_set_style_arc_color(arc1, lv_color_hex(0x00FFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc1, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc1, 30, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_set_clickable(arc1, false);

    label1 = lv_label_create(scr);
    lv_label_set_text(label1, "120\nkm/h");
    lv_obj_set_style_text_color(label1, lv_color_white(), 0);
    // ??????????: ?????????? 14 ?????, ??????? ?????????????? ???? ? sdkconfig
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(label1, arc1, LV_ALIGN_CENTER, 0, 0);

    // ???? 2 (????????)
    arc2 = lv_arc_create(scr);
    lv_obj_set_size(arc2, 400, 400);
    lv_obj_align(arc2, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_arc_set_rotation(arc2, 135);
    lv_arc_set_range(arc2, 0, 8000);
    lv_arc_set_value(arc2, 4000);
    lv_obj_set_style_arc_color(arc2, lv_color_hex(0xFF00FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc2, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc2, 30, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_set_clickable(arc2, false);

    label2 = lv_label_create(scr);
    lv_label_set_text(label2, "4000\nRPM");
    lv_obj_set_style_text_color(label2, lv_color_white(), 0);
    // ??????????: ?????????? 14 ?????
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(label2, arc2, LV_ALIGN_CENTER, 0, 0);

    bsp_display_unlock();
    ESP_LOGI(TAG, "UI Ready! Dashboard is running.");

    // 4. ???????? (????????? ??????)
    int speed = 0;
    int rpm = 0;
    int dir = 1;
    while (1) {
        speed += dir * 5;
        rpm += dir * 150;
        if (speed >= 240 || speed <= 0) dir = -dir;

        if (bsp_display_lock(0)) {
            lv_arc_set_value(arc1, speed);
            lv_arc_set_value(arc2, rpm);
            lv_label_set_text_fmt(label1, "%d\nkm/h", speed);
            lv_label_set_text_fmt(label2, "%d\nRPM", rpm);
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
