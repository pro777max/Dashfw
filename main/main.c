#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_jd9165.h"
#include "lvgl.h"

static const char *TAG = "DASH_JD9165";

static lv_display_t *lvgl_disp = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

// ??????? ????????? ?????? ??? ????????? LVGL ?? ???????
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void init_jd9165_display(void) {
    ESP_LOGI(TAG, "Initializing MIPI DSI Bus (2 lanes)...");
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2, // ?? ????????? ????????: MIPI_2lane
        .phy_clk_src = LCD_PHY_CLK_SRC_PLL_FPLL,
        .lane_bit_rate_mbps = 1000, // 1 Gbps ?? ?????
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus));

    ESP_LOGI(TAG, "Initializing DSI Panel IO...");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_dsi_panel_io_config_t io_config = {
        .dc_gpio_num = -1,
        .cs_gpio_num = -1,
        .pclk_hz = 10000000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dsi(dsi_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Initializing JD9165 Panel...");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 27, // GPIO 27 ??? ?????? (?? ?????????? ?????)
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, // RGB565
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io_handle, &panel_config, &panel_handle));
    
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); // ???????? ???????!
    ESP_LOGI(TAG, "JD9165 Panel Initialized and Turned ON!");
}

static void init_lvgl(void) {
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    // ??????? ????? ? PSRAM (1024 * 100 ???????? * 2 ????? = ~200 ??)
    size_t buffer_size = 1024 * 100 * sizeof(lv_color_t);
    lv_color_t *buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer in PSRAM!");
        return;
    }

    lvgl_disp = lv_display_create(1024, 600);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
    lv_display_set_buffers(lvgl_disp, buf1, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "LVGL Initialized with PSRAM buffer");
}

static void force_backlight(void) {
    ESP_LOGI(TAG, "Forcing backlight on GPIO 23...");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 23),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_23, 1);
    ESP_LOGI(TAG, "Backlight ON");
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting JD9165 Manual Init Test...");

    // 1. ?????????????? ??????? ??????? (?????????? ??????????!)
    init_jd9165_display();
    
    // 2. ???? ????? ?? ????????????
    vTaskDelay(pdMS_TO_TICKS(300));

    // 3. ?????????????? LVGL
    init_lvgl();

    // 4. ???????? ?????????
    force_backlight();

    // 5. ?????? ????-??????? ?????
    ESP_LOGI(TAG, "Drawing RED screen...");
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "JD9165\nWORKS!\n1024x600");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // 6. ?????????????? ???????????
    lv_refr_now(NULL);
    ESP_LOGI(TAG, "If you see RED, we conquered the hardware!");

    // 7. ???? ?????
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
