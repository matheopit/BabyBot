#pragma once
#include "demos/lv_demos.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>

#include "ST7789.h"
#define LVGL_BUF_W 320
#define LVGL_BUF_H 40
#define LVGL_BUF_LEN (LVGL_BUF_W * LVGL_BUF_H)

// #define LVGL_BUF_LEN  (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES)
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

extern lv_disp_draw_buf_t
	disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
extern lv_disp_drv_t disp_drv; // contains callback functions
extern lv_disp_t *disp;

bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
							 esp_lcd_panel_io_event_data_t *edata,
							 void *user_ctx);
void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
				   lv_color_t *color_map);
// Call this function to initialize the screen (must be called in the main
// function) !!!!!
void LVGL_Init(void);