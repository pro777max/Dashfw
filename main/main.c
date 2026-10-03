#include <stdio.h>
#include <string.h>
#include <time.h>
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

/* ????? Prius */
#define C_BG            lv_color_hex(0x000000)
#define C_GREEN         lv_color_hex(0x00ff88)
#define C_GREEN_DIM     lv_color_hex(0x005533)
#define C_RED           lv_color_hex(0xff3333)
#define C_WHITE         lv_color_hex(0xffffff)

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;

/* UI ??????? */
static lv_obj_t *fuel_arc = NULL;
static lv_obj_t *fuel_label = NULL;
static lv_obj_t *temp_arc = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *speed_label = NULL;
static lv_obj_t *odo_label = NULL;
static lv_obj_t *consumption_label = NULL;
static lv_obj_t *shift_label = NULL;
static lv_obj_t *left_arrow = NULL;
static lv_obj_t *right_arrow = NULL;
static lv_obj_t *tach_bar = NULL;
static lv_obj_t *tach_label = NULL;

/* ????????? */
static int fuel_val = 75;       /* 0-100 */
static int temp_val = 50;       /* 0-100, 50 = ????? */
static int speed_val = 0;       /* km/h */
static int odo_val = 222222;    /* km */
static float consumption = 4.2f;
static char gear = 'P';
static bool left_blink = false;
static bool right_blink = false;
static uint8_t blink_state = 0;

/* --- Backlight --- */
static void backlight_init(void) {
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 1000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = DISP_BL_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2, .timer_sel = LEDC_TIMER_2,
        .intr_type = LEDC_INTR_DISABLE, .duty = 819, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

/* --- Display init --- */
static esp_err_t display_init(void) {
    ESP_LOGI(TAG, "Init JD9165...");
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "LDO");

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "DSI");

    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io), TAG, "DBI");

    esp_lcd_dpi_panel_config_t dpi_cfg = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi_cfg.dpi_clock_freq_mhz = 40;

    jd9165_vendor_config_t vendor_cfg = {
        .init_cmds = NULL, .init_cmds_size = 0,
        .mipi_config = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISP_RST_GPIO, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi_io, &panel_cfg, &panel_handle), TAG, "Panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "On");
    return ESP_OK;
}

/* --- Touch init --- */
static esp_err_t touch_init(void) {
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = TOUCH_SDA, .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .intr_priority = 0, .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = 1 },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = 0x5D;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io), TAG, "IO");

    static const esp_lcd_touch_io_gt911_config_t gt911_cfg = { .dev_addr = 0x5D };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISP_H_RES, .y_max = DISP_V_RES,
        .rst_gpio_num = TOUCH_RST, .int_gpio_num = TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data = (void *)&gt911_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp_handle), TAG, "GT911");
    return ESP_OK;
}

/* --- LVGL callbacks --- */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!tp_handle) { data->state = LV_INDEV_STATE_RELEASED; return; }
    esp_lcd_touch_read_data(tp_handle);
    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    esp_err_t ret = esp_lcd_touch_get_data(tp_handle, pts, &cnt, 1);
    if (ret == ESP_OK && cnt > 0) {
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg) { lv_tick_inc(2); }

/* --- Event handlers (?????? C, ??? ?????) --- */
static void fuel_click_cb(lv_event_t *e) {
    fuel_val = (fuel_val + 25) % 101;
    lv_arc_set_value(fuel_arc, fuel_val);
    char buf[32]; snprintf(buf, sizeof(buf), "%d", fuel_val);
    lv_label_set_text(fuel_label, buf);
    ESP_LOGI(TAG, "Fuel: %d", fuel_val);
}

static void temp_click_cb(lv_event_t *e) {
    temp_val = (temp_val + 25) % 101;
    lv_arc_set_value(temp_arc, temp_val);
    char buf[32]; snprintf(buf, sizeof(buf), "%d", temp_val);
    lv_label_set_text(temp_label, buf);
    ESP_LOGI(TAG, "Temp: %d", temp_val);
}

static void speed_click_cb(lv_event_t *e) {
    speed_val = (speed_val + 20) % 201;
    char buf[16]; snprintf(buf, sizeof(buf), "%d", speed_val);
    lv_label_set_text(speed_label, buf);
    ESP_LOGI(TAG, "Speed: %d", speed_val);
}

static void left_click_cb(lv_event_t *e) {
    left_blink = !left_blink;
    lv_obj_set_style_text_color(left_arrow, left_blink ? C_GREEN : C_GREEN_DIM, 0);
    ESP_LOGI(TAG, "Left blink: %s", left_blink ? "ON" : "OFF");
}

static void right_click_cb(lv_event_t *e) {
    right_blink = !right_blink;
    lv_obj_set_style_text_color(right_arrow, right_blink ? C_GREEN : C_GREEN_DIM, 0);
    ESP_LOGI(TAG, "Right blink: %s", right_blink ? "ON" : "OFF");
}

static void shift_click_cb(lv_event_t *e) {
    const char gears[] = "PRNDL";
    int idx = 0;
    for (int i = 0; i < 5; i++) if (gears[i] == gear) { idx = i; break; }
    gear = gears[(idx + 1) % 5];
    char buf[16]; snprintf(buf, sizeof(buf), "SHIFT %c", gear);
    lv_label_set_text(shift_label, buf);
    ESP_LOGI(TAG, "Gear: %c", gear);
}

static void tach_click_cb(lv_event_t *e) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tach_bar);
    lv_anim_set_values(&a, 0, 6000);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_bar_set_value);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "Tach rev");
}

/* --- Timers --- */
static void blink_timer_cb(lv_timer_t *timer) {
    blink_state = !blink_state;
    if (left_blink)
        lv_obj_set_style_text_color(left_arrow, blink_state ? C_GREEN : C_GREEN_DIM, 0);
    if (right_blink)
        lv_obj_set_style_text_color(right_arrow, blink_state ? C_GREEN : C_GREEN_DIM, 0);
}

static void update_timer_cb(lv_timer_t *timer) {
    /* Odometer */
    if (speed_val > 0) odo_val += speed_val / 3600;
    char buf[32];
    snprintf(buf, sizeof(buf), "ODO %d km", odo_val);
    lv_label_set_text(odo_label, buf);

    /* Consumption (????????? ?????????) */
    consumption = 4.0f + ((rand() % 20) / 10.0f);
    snprintf(buf, sizeof(buf), "%.1f km/l", consumption);
    lv_label_set_text(consumption_label, buf);

    /* RPM label */
    int rpm = lv_bar_get_value(tach_bar);
    snprintf(buf, sizeof(buf), "%d", rpm / 1000);
    lv_label_set_text(tach_label, buf);
}

static void lvgl_task(void *arg) {
    while (1) {
        uint32_t t = lv_timer_handler();
        if (t < 5) t = 5;
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}

/* --- Build Prius dashboard --- */
static void create_dashboard(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* === FUEL ARC (????? ????) === */
    fuel_arc = lv_arc_create(scr);
    lv_obj_set_size(fuel_arc, 140, 140);
    lv_obj_align(fuel_arc, LV_ALIGN_LEFT_MID, 20, -40);
    lv_arc_set_range(fuel_arc, 0, 100);
    lv_arc_set_value(fuel_arc, fuel_val);
    lv_arc_set_bg_angles(fuel_arc, 150, 30);
    lv_arc_set_rotation(fuel_arc, 135);
    lv_obj_set_style_arc_width(fuel_arc, 8, 0);
    lv_obj_set_style_arc_color(fuel_arc, C_GREEN_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_color(fuel_arc, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_flag(fuel_arc, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(fuel_arc, fuel_click_cb, LV_EVENT_CLICKED, NULL);

    fuel_label = lv_label_create(scr);
    lv_obj_set_style_text_color(fuel_label, C_GREEN, 0);
    lv_obj_set_style_text_font(fuel_label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(fuel_label, fuel_arc, LV_ALIGN_CENTER, 0, 0);
    char buf[32]; snprintf(buf, sizeof(buf), "%d", fuel_val);
    lv_label_set_text(fuel_label, buf);

    /* ??????? F/E */
    lv_obj_t *f_label = lv_label_create(scr);
    lv_label_set_text(f_label, "F");
    lv_obj_set_style_text_color(f_label, C_GREEN, 0);
    lv_obj_set_style_text_font(f_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(f_label, fuel_arc, LV_ALIGN_TOP_RIGHT, 5, 5);

    lv_obj_t *e_label = lv_label_create(scr);
    lv_label_set_text(e_label, "E");
    lv_obj_set_style_text_color(e_label, C_GREEN, 0);
    lv_obj_set_style_text_font(e_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(e_label, fuel_arc, LV_ALIGN_BOTTOM_RIGHT, 5, -5);

    /* ?????? ???????????? (?????) */
    lv_obj_t *fuel_icon = lv_label_create(scr);
    lv_label_set_text(fuel_icon, "[FUEL]");
    lv_obj_set_style_text_color(fuel_icon, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(fuel_icon, &lv_font_montserrat_10, 0);
    lv_obj_align_to(fuel_icon, fuel_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* === TEMP ARC (?????? ????) === */
    temp_arc = lv_arc_create(scr);
    lv_obj_set_size(temp_arc, 140, 140);
    lv_obj_align(temp_arc, LV_ALIGN_RIGHT_MID, -20, -40);
    lv_arc_set_range(temp_arc, 0, 100);
    lv_arc_set_value(temp_arc, temp_val);
    lv_arc_set_bg_angles(temp_arc, 150, 30);
    lv_arc_set_rotation(temp_arc, 135);
    lv_obj_set_style_arc_width(temp_arc, 8, 0);
    lv_obj_set_style_arc_color(temp_arc, C_GREEN_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_color(temp_arc, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_flag(temp_arc, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(temp_arc, temp_click_cb, LV_EVENT_CLICKED, NULL);

    temp_label = lv_label_create(scr);
    lv_obj_set_style_text_color(temp_label, C_GREEN, 0);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(temp_label, temp_arc, LV_ALIGN_CENTER, 0, 0);
    snprintf(buf, sizeof(buf), "%d", temp_val);
    lv_label_set_text(temp_label, buf);

    lv_obj_t *h_label = lv_label_create(scr);
    lv_label_set_text(h_label, "H");
    lv_obj_set_style_text_color(h_label, C_GREEN, 0);
    lv_obj_set_style_text_font(h_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(h_label, temp_arc, LV_ALIGN_TOP_LEFT, -5, 5);

    lv_obj_t *c_label = lv_label_create(scr);
    lv_label_set_text(c_label, "C");
    lv_obj_set_style_text_color(c_label, C_GREEN, 0);
    lv_obj_set_style_text_font(c_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(c_label, temp_arc, LV_ALIGN_BOTTOM_LEFT, -5, -5);

    lv_obj_t *temp_icon = lv_label_create(scr);
    lv_label_set_text(temp_icon, "[TEMP]");
    lv_obj_set_style_text_color(temp_icon, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(temp_icon, &lv_font_montserrat_10, 0);
    lv_obj_align_to(temp_icon, temp_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* === TURN ARROWS === */
    left_arrow = lv_label_create(scr);
    lv_label_set_text(left_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(left_arrow, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(left_arrow, &lv_font_montserrat_36, 0);
    lv_obj_align(left_arrow, LV_ALIGN_TOP_MID, -80, 80);
    lv_obj_set_flag(left_arrow, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(left_arrow, left_click_cb, LV_EVENT_CLICKED, NULL);

    right_arrow = lv_label_create(scr);
    lv_label_set_text(right_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(right_arrow, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(right_arrow, &lv_font_montserrat_36, 0);
    lv_obj_align(right_arrow, LV_ALIGN_TOP_MID, 80, 80);
    lv_obj_set_flag(right_arrow, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(right_arrow, right_click_cb, LV_EVENT_CLICKED, NULL);

    /* === SPEED (??????? ????? ?? ??????) === */
    speed_label = lv_label_create(scr);
    lv_obj_set_style_text_color(speed_label, C_GREEN, 0);
    lv_obj_set_style_text_font(speed_label, &lv_font_montserrat_48, 0);
    lv_obj_align(speed_label, LV_ALIGN_CENTER, 0, -30);
    lv_label_set_text(speed_label, "0");
    lv_obj_set_flag(speed_label, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(speed_label, speed_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kmh_label = lv_label_create(scr);
    lv_label_set_text(kmh_label, "km/h");
    lv_obj_set_style_text_color(kmh_label, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(kmh_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(kmh_label, speed_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* === SHIFT P === */
    shift_label = lv_label_create(scr);
    lv_obj_set_style_text_color(shift_label, C_GREEN, 0);
    lv_obj_set_style_text_font(shift_label, &lv_font_montserrat_14, 0);
    lv_obj_align(shift_label, LV_ALIGN_CENTER, -100, 40);
    lv_label_set_text(shift_label, "SHIFT P");
    lv_obj_set_flag(shift_label, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(shift_label, shift_click_cb, LV_EVENT_CLICKED, NULL);

    /* === TACHOMETER (?????????????? ????? 0-8) === */
    /* ??????? 0 1 2 3 4 5 6 7 8 */
    int tach_y = 420;
    int tach_x_start = 180;
    int tach_x_end = 840;
    int tach_width = tach_x_end - tach_x_start;

    for (int i = 0; i <= 8; i++) {
        lv_obj_t *lbl = lv_label_create(scr);
        char nbuf[4]; snprintf(nbuf, sizeof(nbuf), "%d", i);
        lv_label_set_text(lbl, nbuf);
        lv_obj_set_style_text_color(lbl, (i >= 7) ? C_RED : C_GREEN, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        int x = tach_x_start + (i * tach_width / 8);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x - 5, tach_y - 25);
    }

    /* x1000r/min ??????? */
    lv_obj_t *rpm_title = lv_label_create(scr);
    lv_label_set_text(rpm_title, "x1000r/min");
    lv_obj_set_style_text_color(rpm_title, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(rpm_title, &lv_font_montserrat_10, 0);
    lv_obj_align(rpm_title, LV_ALIGN_TOP_LEFT, tach_x_end - 60, tach_y - 45);

    /* ??? ????????? */
    tach_bar = lv_bar_create(scr);
    lv_obj_set_size(tach_bar, tach_width, 14);
    lv_obj_align(tach_bar, LV_ALIGN_TOP_LEFT, tach_x_start, tach_y);
    lv_bar_set_range(tach_bar, 0, 8000);
    lv_bar_set_value(tach_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tach_bar, C_GREEN_DIM, 0);
    lv_obj_set_style_bg_color(tach_bar, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_flag(tach_bar, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(tach_bar, tach_click_cb, LV_EVENT_CLICKED, NULL);

    /* ??????? ???? (7-8) ? ????????? ??? ?????? */
    lv_obj_t *red_zone = lv_obj_create(scr);
    lv_obj_set_size(red_zone, tach_width / 8, 14);
    lv_obj_align(red_zone, LV_ALIGN_TOP_LEFT, tach_x_start + 7 * tach_width / 8, tach_y);
    lv_obj_set_style_bg_color(red_zone, C_RED, 0);
    lv_obj_set_style_bg_opa(red_zone, LV_OPA_30, 0);
    lv_obj_set_style_border_width(red_zone, 0, 0);
    lv_obj_set_style_radius(red_zone, 0, 0);

    /* ??????? ???????? RPM */
    tach_label = lv_label_create(scr);
    lv_obj_set_style_text_color(tach_label, C_GREEN, 0);
    lv_obj_set_style_text_font(tach_label, &lv_font_montserrat_20, 0);
    lv_obj_align(tach_label, LV_ALIGN_TOP_LEFT, tach_x_start - 50, tach_y - 5);
    lv_label_set_text(tach_label, "0");

    /* === CONSUMPTION (????? ?????) === */
    consumption_label = lv_label_create(scr);
    lv_obj_set_style_text_color(consumption_label, C_GREEN, 0);
    lv_obj_set_style_text_font(consumption_label, &lv_font_montserrat_14, 0);
    lv_obj_align(consumption_label, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    snprintf(buf, sizeof(buf), "%.1f km/l", consumption);
    lv_label_set_text(consumption_label, buf);

    /* === ODOMETER (?????? ?????) === */
    odo_label = lv_label_create(scr);
    lv_obj_set_style_text_color(odo_label, C_GREEN, 0);
    lv_obj_set_style_text_font(odo_label, &lv_font_montserrat_14, 0);
    lv_obj_align(odo_label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    snprintf(buf, sizeof(buf), "ODO %d km", odo_val);
    lv_label_set_text(odo_label, buf);

    /* Timers */
    lv_timer_create(blink_timer_cb, 500, NULL);
    lv_timer_create(update_timer_cb, 1000, NULL);
}

/* --- MAIN --- */
void app_main(void) {
    ESP_LOGI(TAG, "=== PRIUS DASHBOARD ===");
    backlight_init();
    ESP_ERROR_CHECK(display_init());
    if (touch_init() != ESP_OK) ESP_LOGW(TAG, "Touch failed");

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
    }

    const esp_timer_create_args_t tick_args = { .callback = tick_cb, .name = "lvgl_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000));

    create_dashboard();
    ESP_LOGI(TAG, "Dashboard ready");
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 0);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive");
    }
}


