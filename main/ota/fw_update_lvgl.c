#include "fw_update_lvgl.h"
#include "LVGL_Driver.h"
#include "ST7789.h"
#include "app_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

// Avancement écrit par la tâche de flash, lu par la tâche LVGL
static volatile int flash_percent = 0;
static const char *volatile flash_error = NULL;

static lv_obj_t *update_bar;
static lv_obj_t *update_label;

void fw_update_lvgl_set_percent(int percent) { flash_percent = percent; }

void fw_update_lvgl_set_error(const char *error) { flash_error = error; }

static void update_screen_timer_cb(lv_timer_t *t) {
	if (flash_error) {
		lv_label_set_text_fmt(update_label, "Echec : %s", flash_error);
		lv_obj_set_style_text_color(update_label, lv_color_hex(0xFF5050), 0);
		return;
	}
	lv_bar_set_value(update_bar, flash_percent, LV_ANIM_OFF);
	lv_label_set_text_fmt(update_label, "%d %%", flash_percent);
}

void fw_update_lvgl_show(void) {
	lv_obj_t *scr = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	// Pas d'accents : les polices Montserrat intégrées sont en ASCII
	lv_obj_t *title = lv_label_create(scr);
	lv_label_set_text(title, "Mise a jour\ndu firmware");
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

	update_bar = lv_bar_create(scr);
	lv_obj_set_size(update_bar, 180, 14);
	lv_obj_set_style_bg_color(update_bar, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_border_color(update_bar, lv_color_hex(COLOR_MAIN),
								  LV_PART_MAIN);
	lv_obj_set_style_border_width(update_bar, 1, LV_PART_MAIN);
	lv_obj_set_style_pad_all(update_bar, 3, LV_PART_MAIN);
	lv_obj_set_style_bg_color(update_bar, lv_color_hex(COLOR_MAIN),
							  LV_PART_INDICATOR);
	lv_bar_set_range(update_bar, 0, 100);
	lv_obj_align(update_bar, LV_ALIGN_CENTER, 0, 0);

	update_label = lv_label_create(scr);
	lv_obj_set_style_text_color(update_label, lv_color_hex(COLOR_MAIN), 0);
	lv_label_set_text(update_label, "0 %");
	lv_obj_align(update_label, LV_ALIGN_CENTER, 0, 30);

	lv_scr_load(scr);
	lv_timer_create(update_screen_timer_cb, 200, NULL);
	lv_refr_now(NULL);
	vTaskDelay(pdMS_TO_TICKS(20)); // fin du dernier transfert DMA
	LCD_Display_On();
}

void fw_update_lvgl_loop(void) {
	// Seule boucle LVGL jusqu'au redémarrage (pas de Wi-Fi, NFC ni réveil)
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(10));
		// Le tick LVGL n'avance que si on l'appelle (voir LVGL_Tick_Update) :
		// sans lui, le timer de la barre ne se déclenche jamais
		LVGL_Tick_Update();
		lv_timer_handler();
	}
}
