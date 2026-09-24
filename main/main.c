#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "lvgl.h"

static const char *TAG = "DASH_JD9165";

static lv_display_t *lvgl_disp = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void init_jd9165_display(void) {
    ESP_LOGI(TAG, "Powering on MIPI DSI PHY...");
    // ???????? ??????? PHY (????????? ??? ESP32-P4)
    // ???? ldo ?? ????????, ???? ??? ????? ??????????, ?? ????? ????????
    
    ESP_LOGI(TAG, "Initialize MIPI DSI bus (2 lanes)...");
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install panel IO (DBI)...");
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    ESP_LOGI(TAG, "Install JD9165 panel driver...");
    const esp_lcd_dpi_panel_config_t dpi_config = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    
    jd9165_vendor_config_t vendor_config = {
        .flags = {
            .use_mipi_interface = 1,
        },
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 27, // GPIO 27 ??? ??????
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, // RGB565
        .vendor_config = &vendor_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(mipi_dbi_io, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "JD9165 Panel Initialized and Turned ON!");
}

static void init_lvgl(void) {
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

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

    init_jd9165_display();
    vTaskDelay(pdMS_TO_TICKS(300));
    init_lvgl();
    force_backlight();

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

    lv_refr_now(NULL);
    ESP_LOGI(TAG, "If you see RED, we conquered the hardware!");

    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
