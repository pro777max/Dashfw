#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "FINAL_SWEEP";

void app_main(void) {
    ESP_LOGI(TAG, "step 1: bsp_display_start");
    bsp_display_start();
    
    ESP_LOGI(TAG, "step 2: bsp_display_lock");
    bsp_display_lock(0);
    
    ESP_LOGI(TAG, "step 3: lv_screen_active");
    lv_obj_t *scr = lv_screen_active();
    ESP_LOGI(TAG, "step 3 done, scr=%p", (void*)scr);
    
    ESP_LOGI(TAG, "step 4: set bg color");
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    ESP_LOGI(TAG, "step 5: create label");
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "SWEEP");
    lv_obj_center(lbl);
    
    ESP_LOGI(TAG, "step 6: bsp_display_unlock");
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "step 7: backlight_on");
    esp_err_t bl = bsp_display_backlight_on();
    ESP_LOGI(TAG, "backlight_on: %s", esp_err_to_name(bl));
    
    ESP_LOGI(TAG, "step 8: sweep start");
    const int pins[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
                        21,22,23,24,25,26,27,28,29,30,31,32,33,
                        45,46,47,48,49,50,51,52,53,54};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_num_t p = (gpio_num_t)pins[i];
        gpio_reset_pin(p);
        gpio_set_direction(p, GPIO_MODE_OUTPUT);
        gpio_set_level(p, 1);
        ESP_LOGI(TAG, "PIN %d HIGH", pins[i]);
        vTaskDelay(pdMS_TO_TICKS(2000));
        gpio_set_level(p, 0);
    }
    ESP_LOGI(TAG, "Sweep done");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
