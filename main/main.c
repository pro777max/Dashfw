#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
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
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

static const char *TAG = "DASH";

/* ---------------- Дисплей JD9165 1024x600 MIPI-DSI ---------------- */
#define DISP_H_RES      1024
#define DISP_V_RES      600
#define DISP_RST_GPIO   5
#define DISP_BL_GPIO    23
#define DISP_BL_DUTY    819          /* 10-bit: 819/1023 ~ 80% */
#define DISP_PART_LINES 40           /* высота partial-буфера LVGL */

/* ---------------- Тач GT911 ---------------- */
#define TOUCH_SDA       7
#define TOUCH_SCL       8
#define TOUCH_INT       21
#define TOUCH_RST       22
#define TOUCH_I2C_ADDR  0x5D

/* ---------------- UART2 -> Arduino Mega 2560 ---------------- */
#define UART_PORT_NUM       UART_NUM_2
#define UART_TX_PIN         43      /* ESP32-P4 TX -> Mega Pin19 (RX1) */
#define UART_RX_PIN         44      /* ESP32-P4 RX <- Mega Pin18 (TX1) */
#define UART_BAUD_RATE      115200
#define UART_RX_DRV_BUF     1024    /* драйверный RX-буфер */
#define UART_LINE_MAX       96      /* макс. длина одной строки протокола */
#define UART_RX_TASK_STACK  4096
#define UART_RX_TASK_PRIO   5
/* 1 = слать в Mega "PING:1\n" раз в секунду (диагностика линии TX->Pin19).
   Ставить 0, если на Mega парсер входа не реализован. */
#define UART_TX_HEARTBEAT   0
/* Нет пакетов дольше этого времени -> индикатор "NO LINK" */
#define UART_LINK_TIMEOUT_US 2000000LL

/* ---------------- Цвета ---------------- */
#define C_BG            lv_color_hex(0x000000)
#define C_GREEN         lv_color_hex(0x00ff88)
#define C_GREEN_DIM     lv_color_hex(0x006633)
#define C_GREEN_BRIGHT  lv_color_hex(0x44ffaa)
#define C_RED           lv_color_hex(0xff3333)

/* ---------------- Пределы приборов ---------------- */
#define RPM_MAX         9000
#define SPEED_MAX       220
#define TEMP_MAX        100
#define FUEL_MAX        100

/* ---------------- Глобальные хендлы ---------------- */
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;

/* ---------------- Виджеты ---------------- */
static lv_obj_t *fuel_arc = NULL;
static lv_obj_t *fuel_label = NULL;
static lv_obj_t *temp_arc = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *speed_label = NULL;
static lv_obj_t *kmh_label = NULL;
static lv_obj_t *odo_label = NULL;
static lv_obj_t *consumption_label = NULL;
static lv_obj_t *shift_label = NULL;
static lv_obj_t *left_arrow = NULL;
static lv_obj_t *right_arrow = NULL;
static lv_obj_t *tach_bar = NULL;
static lv_obj_t *tach_label = NULL;
static lv_obj_t *link_label = NULL;

/* ---------------- Состояние панели ---------------- */
static int   fuel_val     = 75;
static int   temp_val     = 50;
static int   speed_val    = 0;
static int   rpm_val      = 0;
static int   odo_val      = 222222;
static float consumption  = 0.0f;
static char  gear         = 'P';
static bool  left_blink   = false;
static bool  right_blink  = false;
static bool  blink_state  = false;
static bool  link_ok      = false;

/* ---------------- Данные UART (общие между задачами) ---------------- */
typedef struct {
    int speed;
    int rpm;
    int temp;
} uart_frame_t;

static portMUX_TYPE       s_uart_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uart_frame_t s_uart_pending;
static volatile bool      s_uart_pending_new = false;
static volatile int64_t   s_uart_last_us     = 0;
static volatile uint32_t  s_uart_frames_ok   = 0;
static volatile uint32_t  s_uart_frames_bad  = 0;

/* ================================================================= */
/*                          ПОДСВЕТКА                                */
/* ================================================================= */
static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {
        .gpio_num   = DISP_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_2,
        .timer_sel  = LEDC_TIMER_2,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = DISP_BL_DUTY,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

/* ================================================================= */
/*                     ДИСПЛЕЙ JD9165 (MIPI-DSI)                     */
/* ================================================================= */
static esp_err_t display_init(void)
{
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

    esp_lcd_dpi_panel_config_t dpi_cfg =
        JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi_cfg.dpi_clock_freq_mhz = 40;

    jd9165_vendor_config_t vendor_cfg = {
        .init_cmds      = NULL,
        .init_cmds_size = 0,
        .mipi_config    = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISP_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi_io, &panel_cfg, &panel_handle), TAG, "Panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "On");
    return ESP_OK;
}

/* ================================================================= */
/*                          ТАЧ GT911                                */
/* ================================================================= */
static esp_err_t touch_init(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = TOUCH_SDA,
        .scl_io_num        = TOUCH_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = 1 },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = TOUCH_I2C_ADDR;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io), TAG, "IO");

    static const esp_lcd_touch_io_gt911_config_t gt911_cfg = { .dev_addr = TOUCH_I2C_ADDR };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max          = DISP_H_RES,
        .y_max          = DISP_V_RES,
        .rst_gpio_num   = TOUCH_RST,
        .int_gpio_num   = TOUCH_INT,
        .levels         = { .reset = 0, .interrupt = 0 },
        .flags          = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data    = (void *)&gt911_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp_handle), TAG, "GT911");
    return ESP_OK;
}

/* ================================================================= */
/*                UART2: ПРИЁМ "SPEED:%d,RPM:%d,TEMP:%d\n"           */
/* ================================================================= */
static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Разбор одной строки (без '\n' и без '\r'). Возврат true = кадр принят. */
static bool uart_parse_line(const char *line)
{
    int s = 0, r = 0, t = 0;
    if (sscanf(line, "SPEED:%d,RPM:%d,TEMP:%d", &s, &r, &t) != 3) {
        return false;
    }
    uart_frame_t f = {
        .speed = clamp_int(s, 0, SPEED_MAX),
        .rpm   = clamp_int(r, 0, RPM_MAX),
        .temp  = clamp_int(t, 0, TEMP_MAX),
    };
    taskENTER_CRITICAL(&s_uart_mux);
    s_uart_pending     = f;
    s_uart_pending_new = true;
    s_uart_last_us     = esp_timer_get_time();
    s_uart_frames_ok++;
    taskEXIT_CRITICAL(&s_uart_mux);
    return true;
}

static void uart_rx_task(void *arg)
{
    (void)arg;
    static uint8_t rx[512];
    size_t len = 0;

    for (;;) {
        int n = uart_read_bytes(UART_PORT_NUM, rx + len, sizeof(rx) - len,
                                pdMS_TO_TICKS(20));
        if (n <= 0) {
            continue;
        }
        len += (size_t)n;

        uint8_t *line = rx;
        uint8_t *end  = (uint8_t *)memchr(line, '\n', len);
        while (end != NULL) {
            size_t llen = (size_t)(end - line);
            if (llen > 0 && line[llen - 1] == '\r') {
                llen--;                     /* поддержка CRLF */
            }
            if (llen > 0 && llen < UART_LINE_MAX) {
                char buf[UART_LINE_MAX];
                memcpy(buf, line, llen);
                buf[llen] = '\0';
                if (!uart_parse_line(buf)) {
                    taskENTER_CRITICAL(&s_uart_mux);
                    s_uart_frames_bad++;
                    taskEXIT_CRITICAL(&s_uart_mux);
                    ESP_LOGW(TAG, "UART garbage: %s", buf);
                }
            } else if (llen >= UART_LINE_MAX) {
                ESP_LOGW(TAG, "UART line too long, dropped");
            }
            size_t consumed = (size_t)(end - line) + 1;
            len  -= consumed;
            line += consumed;
            if (len > 0) {
                memmove(rx, line, len);
                line = rx;
            }
            end = (uint8_t *)memchr(line, '\n', len);
        }

        if (len >= sizeof(rx)) {            /* мусор без '\n' -> сброс */
            ESP_LOGW(TAG, "UART overflow, buffer reset");
            len = 0;
        }

#if UART_TX_HEARTBEAT
        static int64_t last_ping = 0;
        int64_t now = esp_timer_get_time();
        if (now - last_ping >= 1000000LL) {
            last_ping = now;
            uart_write_bytes(UART_PORT_NUM, "PING:1\n", 7);
        }
#endif
    }
}

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_RX_DRV_BUF, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
    ESP_LOGI(TAG, "UART%d ready: TX=GPIO%d RX=GPIO%d @ %d",
             (int)UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
}

/* ================================================================= */
/*                     LVGL: FLUSH / TOUCH / TICK                    */
/* ================================================================= */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (tp_handle == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    esp_lcd_touch_read_data(tp_handle);

    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    esp_err_t ret = esp_lcd_touch_get_data(tp_handle, pts, &cnt, 1);
    if (ret == ESP_OK && cnt > 0) {
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

/* ================================================================= */
/*                    ТАЧ-ОБРАБОТЧИКИ (отладка)                      */
/* ================================================================= */
static void fuel_click_cb(lv_event_t *e)
{
    (void)e;
    fuel_val = (fuel_val + 25) % (FUEL_MAX + 1);
    lv_arc_set_value(fuel_arc, fuel_val);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", fuel_val);
    lv_label_set_text(fuel_label, buf);
    ESP_LOGI(TAG, "Fuel: %d", fuel_val);
}

static void temp_click_cb(lv_event_t *e)
{
    (void)e;
    temp_val = (temp_val + 25) % (TEMP_MAX + 1);
    lv_arc_set_value(temp_arc, temp_val);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", temp_val);
    lv_label_set_text(temp_label, buf);
    ESP_LOGI(TAG, "Temp: %d", temp_val);
}

static void speed_click_cb(lv_event_t *e)
{
    (void)e;
    speed_val = (speed_val + 20) % (SPEED_MAX + 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", speed_val);
    lv_label_set_text(speed_label, buf);
    ESP_LOGI(TAG, "Speed: %d", speed_val);
}

static void left_click_cb(lv_event_t *e)
{
    (void)e;
    left_blink = !left_blink;
    lv_obj_set_style_text_color(left_arrow, left_blink ? C_GREEN : C_GREEN_DIM, 0);
    ESP_LOGI(TAG, "Left blink: %s", left_blink ? "ON" : "OFF");
}

static void right_click_cb(lv_event_t *e)
{
    (void)e;
    right_blink = !right_blink;
    lv_obj_set_style_text_color(right_arrow, right_blink ? C_GREEN : C_GREEN_DIM, 0);
    ESP_LOGI(TAG, "Right blink: %s", right_blink ? "ON" : "OFF");
}

static void shift_click_cb(lv_event_t *e)
{
    (void)e;
    static const char gears[] = "PRNDL";
    int idx = 0;
    for (int i = 0; i < 5; i++) {
        if (gears[i] == gear) { idx = i; break; }
    }
    gear = gears[(idx + 1) % 5];
    char buf[16];
    snprintf(buf, sizeof(buf), "SHIFT %c", gear);
    lv_label_set_text(shift_label, buf);
    ESP_LOGI(TAG, "Gear: %c", gear);
}

/* Правильный exec-callback для lv_anim: сигнатура void(void*, int32_t).
   Прямой каст lv_bar_set_value (3 аргумента) недопустим - UB. */
static void tach_anim_exec_cb(void *var, int32_t value)
{
    lv_bar_set_value((lv_obj_t *)var, value, LV_ANIM_OFF);
}

static void tach_click_cb(lv_event_t *e)
{
    (void)e;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tach_bar);
    lv_anim_set_exec_cb(&a, tach_anim_exec_cb);
    lv_anim_set_values(&a, 0, 6000);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "Tach sweep");
}

/* ================================================================= */
/*                        LVGL-ТАЙМЕРЫ                               */
/* ================================================================= */
static void blink_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    blink_state = !blink_state;
    if (left_blink) {
        lv_obj_set_style_text_color(left_arrow, blink_state ? C_GREEN : C_GREEN_DIM, 0);
    }
    if (right_blink) {
        lv_obj_set_style_text_color(right_arrow, blink_state ? C_GREEN : C_GREEN_DIM, 0);
    }
}

static void value_update_cb(lv_timer_t *timer)
{
    (void)timer;
    char buf[48];

    /* --- 1. Кадр из UART --- */
    uart_frame_t f = { .speed = 0, .rpm = 0, .temp = 0 };
    bool have_frame = false;
    int64_t last_us = 0;
    uint32_t ok = 0, bad = 0;

    taskENTER_CRITICAL(&s_uart_mux);
    if (s_uart_pending_new) {
        f = s_uart_pending;
        s_uart_pending_new = false;
        have_frame = true;
    }
    last_us = s_uart_last_us;
    ok  = s_uart_frames_ok;
    bad = s_uart_frames_bad;
    taskEXIT_CRITICAL(&s_uart_mux);

    if (have_frame) {
        speed_val = f.speed;
        rpm_val   = f.rpm;
        temp_val  = f.temp;

        snprintf(buf, sizeof(buf), "%d", speed_val);
        lv_label_set_text(speed_label, buf);

        lv_bar_set_value(tach_bar, rpm_val, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d", rpm_val);
        lv_label_set_text(tach_label, buf);
        lv_obj_set_style_text_color(tach_label, C_GREEN_BRIGHT, 0);

        lv_arc_set_value(temp_arc, temp_val);
        snprintf(buf, sizeof(buf), "%d", temp_val);
        lv_label_set_text(temp_label, buf);
    }

    /* --- 2. Контроль связи с Mega --- */
    bool now_ok = (last_us != 0) &&
                  ((esp_timer_get_time() - last_us) < UART_LINK_TIMEOUT_US);
    if (now_ok != link_ok) {
        link_ok = now_ok;
        if (link_ok) {
            lv_label_set_text(link_label, "");
            lv_obj_add_flag(link_label, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "UART link OK (frames ok=%" PRIu32 " bad=%" PRIu32 ")", ok, bad);
        } else {
            lv_obj_remove_flag(link_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(link_label, "%s NO LINK  ok:%" PRIu32 " err:%" PRIu32,
                                  LV_SYMBOL_WARNING, ok, bad);
            lv_label_set_text(tach_label, "--");
            lv_obj_set_style_text_color(tach_label, C_RED, 0);
            ESP_LOGW(TAG, "UART link LOST (frames ok=%" PRIu32 " bad=%" PRIu32 ")", ok, bad);
        }
    }

    /* --- 3. Одометр: тик 100 мс -> км = speed * 0.1 / 3600 --- */
    if (speed_val > 0) {
        odo_val += (int)(((float)speed_val * 0.1f) / 3600.0f + 0.5f);
    }
    snprintf(buf, sizeof(buf), "ODO %d km", odo_val);
    lv_label_set_text(odo_label, buf);

    /* --- 4. Расход: грубая модель по RPM/скорости --- */
    if (speed_val >= 5) {
        consumption = ((float)rpm_val * 0.025f) / (float)speed_val;
    } else {
        consumption = (float)rpm_val * 0.0004f;      /* холостой ход */
    }
    if (consumption > 99.9f) consumption = 99.9f;
    if (consumption < 0.0f)  consumption = 0.0f;
    lv_label_set_text_fmt(consumption_label, "%.1f L/100km", (double)consumption);
}

/* ================================================================= */
/*                        ЗАДАЧА LVGL                                */
/* ================================================================= */
static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t t = lv_timer_handler();
        if (t < 5) t = 5;
        if (t > 100) t = 100;
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}

/* ================================================================= */
/*                       СБОРКА ИНТЕРФЕЙСА                           */
/* ================================================================= */
static void create_dashboard(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Поворотники */
    left_arrow = lv_label_create(scr);
    lv_label_set_text(left_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(left_arrow, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(left_arrow, &lv_font_montserrat_48, 0);
    lv_obj_align(left_arrow, LV_ALIGN_TOP_MID, -100, 40);
    lv_obj_set_clickable(left_arrow, true);
    lv_obj_add_event_cb(left_arrow, left_click_cb, LV_EVENT_CLICKED, NULL);

    right_arrow = lv_label_create(scr);
    lv_label_set_text(right_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(right_arrow, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(right_arrow, &lv_font_montserrat_48, 0);
    lv_obj_align(right_arrow, LV_ALIGN_TOP_MID, 100, 40);
    lv_obj_set_clickable(right_arrow, true);
    lv_obj_add_event_cb(right_arrow, right_click_cb, LV_EVENT_CLICKED, NULL);

    /* Скорость */
    speed_label = lv_label_create(scr);
    lv_obj_set_style_text_color(speed_label, C_GREEN_BRIGHT, 0);
    lv_obj_set_style_text_font(speed_label, &lv_font_montserrat_48, 0);
    lv_obj_align(speed_label, LV_ALIGN_CENTER, 0, -40);
    lv_label_set_text(speed_label, "0");
    lv_obj_set_clickable(speed_label, true);
    lv_obj_add_event_cb(speed_label, speed_click_cb, LV_EVENT_CLICKED, NULL);

    kmh_label = lv_label_create(scr);
    lv_label_set_text(kmh_label, "km/h");
    lv_obj_set_style_text_color(kmh_label, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(kmh_label, &lv_font_montserrat_24, 0);
    lv_obj_align_to(kmh_label, speed_label, LV_ALIGN_OUT_RIGHT_MID, 15, 10);

    /* Селектор передач */
    shift_label = lv_label_create(scr);
    lv_obj_set_style_text_color(shift_label, C_GREEN, 0);
    lv_obj_set_style_text_font(shift_label, &lv_font_montserrat_36, 0);
    lv_obj_align(shift_label, LV_ALIGN_CENTER, 0, 60);
    lv_label_set_text(shift_label, "SHIFT P");
    lv_obj_set_clickable(shift_label, true);
    lv_obj_add_event_cb(shift_label, shift_click_cb, LV_EVENT_CLICKED, NULL);

    /* Топливо */
    fuel_arc = lv_arc_create(scr);
    lv_obj_set_size(fuel_arc, 160, 160);
    lv_obj_align(fuel_arc, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_arc_set_range(fuel_arc, 0, FUEL_MAX);
    lv_arc_set_value(fuel_arc, fuel_val);
    lv_arc_set_bg_angles(fuel_arc, 150, 30);
    lv_arc_set_rotation(fuel_arc, 135);
    lv_obj_set_style_arc_width(fuel_arc, 16, 0);
    lv_obj_set_style_arc_color(fuel_arc, C_GREEN_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_color(fuel_arc, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(fuel_arc, true, 0);
    lv_obj_set_clickable(fuel_arc, true);
    lv_obj_add_event_cb(fuel_arc, fuel_click_cb, LV_EVENT_CLICKED, NULL);

    fuel_label = lv_label_create(scr);
    lv_obj_set_style_text_color(fuel_label, C_GREEN_BRIGHT, 0);
    lv_obj_set_style_text_font(fuel_label, &lv_font_montserrat_36, 0);
    lv_obj_align_to(fuel_label, fuel_arc, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text_fmt(fuel_label, "%d", fuel_val);

    /* Температура */
    temp_arc = lv_arc_create(scr);
    lv_obj_set_size(temp_arc, 160, 160);
    lv_obj_align(temp_arc, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_arc_set_range(temp_arc, 0, TEMP_MAX);
    lv_arc_set_value(temp_arc, temp_val);
    lv_arc_set_bg_angles(temp_arc, 150, 30);
    lv_arc_set_rotation(temp_arc, 135);
    lv_obj_set_style_arc_width(temp_arc, 16, 0);
    lv_obj_set_style_arc_color(temp_arc, C_GREEN_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_color(temp_arc, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(temp_arc, true, 0);
    lv_obj_set_clickable(temp_arc, true);
    lv_obj_add_event_cb(temp_arc, temp_click_cb, LV_EVENT_CLICKED, NULL);

    temp_label = lv_label_create(scr);
    lv_obj_set_style_text_color(temp_label, C_GREEN_BRIGHT, 0);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_36, 0);
    lv_obj_align_to(temp_label, temp_arc, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text_fmt(temp_label, "%d", temp_val);

    /* Тахометр */
    lv_obj_t *rpm_title = lv_label_create(scr);
    lv_label_set_text(rpm_title, "RPM");
    lv_obj_set_style_text_color(rpm_title, C_GREEN_DIM, 0);
    lv_obj_set_style_text_font(rpm_title, &lv_font_montserrat_24, 0);
    lv_obj_align(rpm_title, LV_ALIGN_BOTTOM_MID, 0, -130);

    tach_bar = lv_bar_create(scr);
    lv_obj_set_size(tach_bar, 700, 32);
    lv_obj_align(tach_bar, LV_ALIGN_BOTTOM_MID, 0, -90);
    lv_bar_set_range(tach_bar, 0, RPM_MAX);
    lv_bar_set_value(tach_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tach_bar, C_GREEN_DIM, 0);
    lv_obj_set_style_bg_color(tach_bar, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(tach_bar, 16, 0);
    lv_obj_set_clickable(tach_bar, true);
    lv_obj_add_event_cb(tach_bar, tach_click_cb, LV_EVENT_CLICKED, NULL);

    tach_label = lv_label_create(scr);
    lv_obj_set_style_text_color(tach_label, C_GREEN_BRIGHT, 0);
    lv_obj_set_style_text_font(tach_label, &lv_font_montserrat_36, 0);
    lv_obj_align_to(tach_label, tach_bar, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(tach_label, "0");

    /* Индикатор связи с Arduino Mega */
    link_label = lv_label_create(scr);
    lv_obj_set_style_text_color(link_label, C_RED, 0);
    lv_obj_set_style_text_font(link_label, &lv_font_montserrat_24, 0);
    lv_obj_align(link_label, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(link_label, "");
    lv_obj_add_flag(link_label, LV_OBJ_FLAG_HIDDEN);

    /* Расход и одометр */
    consumption_label = lv_label_create(scr);
    lv_obj_set_style_text_color(consumption_label, C_GREEN, 0);
    lv_obj_set_style_text_font(consumption_label, &lv_font_montserrat_20, 0);
    lv_obj_align(consumption_label, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_label_set_text(consumption_label, "0.0 L/100km");

    odo_label = lv_label_create(scr);
    lv_obj_set_style_text_color(odo_label, C_GREEN, 0);
    lv_obj_set_style_text_font(odo_label, &lv_font_montserrat_20, 0);
    lv_obj_align(odo_label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_label_set_text_fmt(odo_label, "ODO %d km", odo_val);

    lv_timer_create(blink_timer_cb, 500, NULL);
    lv_timer_create(value_update_cb, 100, NULL);
}

/* ================================================================= */
/*                            APP_MAIN                               */
/* ================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== DASHFW: ESP32-P4 + JD9165 1024x600 + GT911 + UART2 ===");

    backlight_init();
    ESP_ERROR_CHECK(display_init());

    esp_err_t tp_err = touch_init();
    if (tp_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed: %s", esp_err_to_name(tp_err));
    }

    uart_init();
    BaseType_t rx_created = xTaskCreate(uart_rx_task, "uart_rx", UART_RX_TASK_STACK,
                                        NULL, UART_RX_TASK_PRIO, NULL);
    if (rx_created != pdPASS) {
        ESP_LOGE(TAG, "uart_rx task create failed");
    }

    lv_init();
    lv_display_t *disp = lv_display_create(DISP_H_RES, DISP_V_RES);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    size_t buf_size = (size_t)DISP_H_RES * DISP_PART_LINES * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "LVGL buffers not allocated (%u bytes each), fallback to internal RAM",
                 (unsigned)buf_size);
        if (buf1 == NULL) buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (buf2 == NULL) buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_ERROR_CHECK((buf1 && buf2) ? ESP_OK : ESP_ERR_NO_MEM);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "LVGL buffers: 2 x %u bytes in PSRAM", (unsigned)buf_size);

    if (tp_handle != NULL) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_user_data(indev, tp_handle);
    } else {
        ESP_LOGW(TAG, "Touch disabled: no input device");
    }

    const esp_timer_create_args_t tick_args = { .callback = tick_cb, .name = "lvgl_tick" };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000));   /* 2 мс */

    create_dashboard();
    ESP_LOGI(TAG, "Dashboard ready");

    BaseType_t lv_created = xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192,
                                                    NULL, 5, NULL, 0);
    if (lv_created != pdPASS) {
        ESP_LOGE(TAG, "lvgl task create failed");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive | frames ok=%" PRIu32 " bad=%" PRIu32 " | last rx %" PRId64 " us ago",
                 s_uart_frames_ok, s_uart_frames_bad,
                 (s_uart_last_us == 0) ? (int64_t)-1 : (esp_timer_get_time() - s_uart_last_us));
    }
}