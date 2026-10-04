/*
 * Écran « Version » : affiche la version du firmware (version.txt, celle
 * comparée aux releases GitHub par la mise à jour). Ouvert depuis l'écran
 * « Paramètres », bouton « Retour » pour y revenir.
 */
#include "version_screen.h"
#include "app_manager.h"
#include "esp_app_desc.h"
#include "lvgl.h"
#include "settings_menu.h"

static lv_obj_t *version_screen = NULL;

static void btn_back_event(lv_event_t *e) {
	app_manager_choose_frame(FRAME_SETTINGS);
}

void version_screen_create(void) {
	if (version_screen == NULL) {
		version_screen = settings_create_screen("Version");

		lv_obj_t *label = lv_label_create(version_screen);
		lv_label_set_text(label, esp_app_get_description()->version);
		lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MAIN), 0);
		lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
		lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);

		lv_obj_t *btn =
			settings_create_button(version_screen, "Retour", btn_back_event);
		lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
	}
	lv_scr_load(version_screen);
}
