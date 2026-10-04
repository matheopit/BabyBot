/*
 * Écran « Paramètres » : choix Réveil / Version.
 * Style fil de fer, comme le menu camembert : fond noir, contours COLOR_MAIN.
 * Toucher le fond ouvre le menu camembert.
 */
#include "settings_menu.h"
#include "app_manager.h"
#include "draw_function.h"
#include "lvgl.h"

static lv_obj_t *settings_screen = NULL;

// Écran noir 240x320 avec le titre en haut et la zone tactile du menu
lv_obj_t *settings_create_screen(const char *title) {
	lv_obj_t *scr = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	// Créée en premier pour rester sous les boutons
	create_full_click_zone(scr);

	lv_obj_t *label = lv_label_create(scr);
	lv_label_set_text(label, title);
	lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
	lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
	return scr;
}

// Bouton fil de fer : contour COLOR_MAIN, rempli pendant l'appui
lv_obj_t *settings_create_button(lv_obj_t *parent, const char *text,
								 lv_event_cb_t cb) {
	lv_color_t color = lv_color_hex(COLOR_MAIN);

	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, 180, 60);
	lv_obj_set_style_bg_color(btn, color, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
	lv_obj_set_style_border_color(btn, color, 0);
	lv_obj_set_style_border_width(btn, 2, 0);
	lv_obj_set_style_radius(btn, 8, 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *label = lv_label_create(btn);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, color, 0);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
	lv_obj_center(label);
	return btn;
}

static void btn_alarm_event(lv_event_t *e) {
	app_manager_choose_frame(FRAME_WAKEUP_SETTINGS);
}

static void btn_version_event(lv_event_t *e) {
	app_manager_choose_frame(FRAME_VERSION);
}

void settings_menu_create(void) {
	if (settings_screen == NULL) {
		settings_screen = settings_create_screen("Parametres");

		lv_obj_t *btn =
			settings_create_button(settings_screen, "Reveil", btn_alarm_event);
		lv_obj_align(btn, LV_ALIGN_CENTER, 0, -40);

		btn = settings_create_button(settings_screen, "Version",
									 btn_version_event);
		lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
	}
	lv_scr_load(settings_screen);
}
