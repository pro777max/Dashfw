#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "driver/ledc.h"

void app_main(void)
{
    // ?????? ????????? ????????? (?? ????? ????? ??? GPIO 23, ? ?? 26)
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_2, .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 1000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t c = { .gpio_num = 23, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_2, .timer_sel = LEDC_TIMER_2, .intr_type = LEDC_INTR_DISABLE, .duty = 819, .hpoint = 0 };
    ledc_channel_config(&c);

    bsp_display_start();
    
    ESP_LOGI("DASH", "Display LVGL test");
    if (bsp_display_lock(1000)) {
        lv_obj_t *scr = lv_screen_active();
        
        lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "DASH OK");
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_center(label);

        bsp_display_unlock();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
