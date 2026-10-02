#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
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

    ESP_LOGI(TAG, "display start");
    bsp_display_start();
    ESP_LOGI(TAG, "display started");

    // ???????? handles ??????? ??? ??????? ???????
    bsp_lcd_handles_t lcd_handles;
    bsp_display_config_t disp_cfg = {
        .hdmi_resolution = BSP_HDMI_RES_NONE,
        .dsi_bus = {
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = 1000,
        },
    };
    esp_err_t ret = bsp_display_new_with_handles(&disp_cfg, &lcd_handles);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Got LCD handles: panel=%p, io=%p", lcd_handles.panel, lcd_handles.io);
        
        // ?????? ????: ?????? ????? ????????????? 200x100 ? ??????
        ESP_LOGI(TAG, "Drawing white rectangle directly to display...");
        uint16_t white_buf[200 * 100];
        memset(white_buf, 0xFF, sizeof(white_buf));  // ????? ???? ? RGB565
        
        // ?????????? ?????? ?????? 1024x600
        int x_start = (1024 - 200) / 2;
        int y_start = (600 - 100) / 2;
        
        ret = esp_lcd_panel_draw_bitmap(lcd_handles.panel, x_start, y_start, 
                                        x_start + 200, y_start + 100, white_buf);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Direct draw SUCCESS - check if white rectangle is visible!");
        } else {
            ESP_LOGE(TAG, "Direct draw FAILED: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "bsp_display_new_with_handles failed: %s", esp_err_to_name(ret));
    }

    // ?????? LVGL
    if (bsp_display_lock(1000)) {
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
        lv_label_set_text(tt, "LVGL RENDER OK");
        lv_obj_set_style_text_color(tt, lv_color_black(), 0);
        lv_obj_center(tt);

        lv_refr_now(NULL);
        bsp_display_unlock();
        ESP_LOGI(TAG, "LVGL canary drawn");
    }

    int sec = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sec++;
        if (sec % 10 == 0) ESP_LOGI(TAG, "alive %d sec", sec);
    }
}
