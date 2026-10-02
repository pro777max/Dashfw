#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9165.h"
#include "esp_ldo_regulator.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

static const char *TAG = "DASH";

#define DISP_H_RES      1024
#define DISP_V_RES      600
#define DISP_RST_GPIO   5
#define DISP_BL_GPIO    23
#define TOUCH_SDA       7
#define TOUCH_SCL       8
#define TOUCH_INT       21
#define TOUCH_RST       22

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;

static void backlight_init(void) {
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = DISP_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_2,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 819, // 80%
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    ESP_LOGI(TAG, "Backlight ON (80%%)");
}

static esp_err_t display_init(void) {
    ESP_LOGI(TAG, "Initializing MIPI-DSI display (JD9165)...");

    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "LDO failed");

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "DSI bus failed");

    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io), TAG, "DBI IO failed");

    esp_lcd_dpi_panel_config_t dpi_cfg = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    // ??????? ???????, ????? ???????? underrun ??? ?????? ? PSRAM
    dpi_cfg.dpi_clock_freq_mhz = 40; 

    jd9165_vendor_config_t vendor_cfg = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .mipi_config = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISP_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi_io, &panel_cfg, &panel_handle), TAG, "Panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Display on failed");

    ESP_LOGI(TAG, "Display initialized: %dx%d", DISP_H_RES, DISP_V_RES);
    return ESP_OK;
}

static esp_err_t touch_init(void) {
    ESP_LOGI(TAG, "Initializing GT911 touch...");

    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = 1 },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C bus failed");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = 0x5D;
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io), TAG, "Touch IO failed");

    static const esp_lcd_touch_io_gt911_config_t gt911_cfg = { .dev_addr = 0x5D };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISP_H_RES,
        .y_max = DISP_V_RES,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data = (void *)&gt911_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp_handle), TAG, "GT911 failed");

    ESP_LOGI(TAG, "Touch initialized successfully");
    return ESP_OK;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!tp_handle) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    esp_lcd_touch_read_data(tp_handle);
    
    esp_lcd_touch_point_data_t pts[1];
    uint8_t touch_cnt = 0;
    // ?????????? ?????????? API ?????? ??????????? esp_lcd_touch_get_coordinates
    bool pressed = esp_lcd_touch_get_data(tp_handle, pts, &touch_cnt, 1);
    
    if (pressed && touch_cnt > 0) {
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ?????????? C-?????? ??? ????????? ??????? (?????? C++ ??????)
static void box_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "TOUCH DETECTED ON BOX!");
    lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_hex(0xff0000), 0);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void tick_cb(void *arg) { 
    lv_tick_inc(2); 
}

static void lvgl_task(void *arg) {
    while (1) {
        uint32_t t = lv_timer_handler();
        if (t < 5) t = 5;
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== DASH: Guition JC1060P470C starting ===");

    backlight_init();
    ESP_ERROR_CHECK(display_init());
    
    esp_err_t tp_err = touch_init();
    if (tp_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed: %s (continuing without touch)", esp_err_to_name(tp_err));
    }

    lv_init();
    lv_display_t *disp = lv_display_create(DISP_H_RES, DISP_V_RES);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    size_t buf_size = DISP_H_RES * 40 * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    if (tp_handle) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_user_data(indev, tp_handle);
        ESP_LOGI(TAG, "LVGL touch indev registered");
    }

    const esp_timer_create_args_t tick_args = { .callback = tick_cb, .name = "lvgl_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000));

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 500, 150);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x00cc00), 0); // ??????? ?? ?????????
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, "DASH WORKS!\nTouch to test");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

    // ???????????? C-??????? ??? ?????????? ???????
    lv_obj_add_event_cb(box, box_click_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "UI drawn, starting LVGL task");
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 0);

    int sec = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sec++;
        if (sec % 10 == 0) ESP_LOGI(TAG, "alive %d sec", sec);
    }
}
