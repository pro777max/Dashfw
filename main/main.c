#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH_ABSOLUTE";

void app_main(void) {
    ESP_LOGI(TAG, "Starting Absolute Display Test...");

    // 1. ????????????? ????? BSP
    ESP_LOGI(TAG, "Init Display via BSP...");
    bsp_display_start();
    
    // 2. ?????????? ?????: ??????? ???????? ??? ?????? ?????? ?? ????????? ???
    ESP_LOGI(TAG, "Waiting 1000ms for panel to wake up...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. ???????? ????????? ?? GPIO 23 (??????? HIGH)
    ESP_LOGI(TAG, "Forcing backlight GPIO 23 to HIGH...");
    gpio_reset_pin(GPIO_NUM_23);
    gpio_set_direction(GPIO_NUM_23, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_23, 1);

    // 4. ?????? ????? ????? LVGL
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0); // ????-???????
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "ABSOLUTE\nTEST\nRED\nSCREEN");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_invalidate(scr);
    bsp_display_unlock();
    
    // 5. ?????????????? ??????????? ????????? ??? ? ?????????
    ESP_LOGI(TAG, "Forcing LVGL redraw cycles...");
    for (int i = 0; i < 5; i++) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Test complete. Screen should be RED.");

    // 6. ???? ?????
    int counter = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        counter++;
        if (counter % 5 == 0) {
            ESP_LOGI(TAG, "System Alive... %d sec", counter);
        }
    }
}
