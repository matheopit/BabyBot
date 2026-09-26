/**
 * @file nfc_popup.c
 * @brief Popup modale ouverte quand un tag NFC connu est posé, quel que soit
 *        l'écran affiché. Deux boutons :
 *          - « Jouer »  : joue le mp3 du tag (arrêté quand le tag est retiré)
 *          - « Wakeup » : choisit ce mp3 comme sonnerie du réveil
 *
 * Style fil de fer, comme le menu camembert : fond noir, contours COLOR_MAIN.
 * La popup est créée sur lv_layer_top() pour rester au-dessus de l'écran
 * actif, même si celui-ci change.
 */

#include "nfc_popup.h"
#include "LVGL_Driver.h"
#include "PCM5101.h"
#include "app_manager.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "NFC_POPUP";

#define POPUP_W 200
#define POPUP_H 150
#define POPUP_BTN_W 80
#define POPUP_BTN_H 50

static lv_obj_t *popup_bg = NULL;
static char popup_path[ALARM_SOUND_PATH_LEN];
static bool music_playing = false; // musique lancée par « Jouer »

static void nfc_popup_close(void) {
	if (popup_bg == NULL)
		return;
	lv_obj_del(popup_bg);
	popup_bg = NULL;
}

static void play_btn_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	ESP_LOGI(TAG, "Lecture de %s", popup_path);
	Play_Music_ex(popup_path);
	music_playing = true;
	nfc_popup_close();
}

static void alarm_btn_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	alarm_t *alarm = getAlarm();
	strncpy(alarm->sound, popup_path, sizeof(alarm->sound) - 1);
	alarm->sound[sizeof(alarm->sound) - 1] = '\0';
	ESP_LOGI(TAG, "Sonnerie du réveil : %s", alarm->sound);
	set_wakeup_config();
	nfc_popup_close();
}

// Clic en dehors du panneau : ferme sans rien faire
static void bg_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	if (lv_event_get_target(e) != popup_bg)
		return;
	nfc_popup_close();
}

static lv_obj_t *popup_btn_create(lv_obj_t *parent, const char *text,
								  lv_event_cb_t cb) {
	lv_color_t color = lv_color_hex(COLOR_MAIN);

	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_remove_style_all(btn);
	lv_obj_set_size(btn, POPUP_BTN_W, POPUP_BTN_H);
	lv_obj_set_style_radius(btn, 8, 0);
	lv_obj_set_style_border_width(btn, 2, 0);
	lv_obj_set_style_border_color(btn, color, 0);
	lv_obj_set_style_bg_color(btn, color, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *lbl = lv_label_create(btn);
	lv_label_set_text(lbl, text);
	lv_obj_set_style_text_color(lbl, color, 0);
	lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_center(lbl);
	return btn;
}

void nfc_popup_open(const char *path) {
	lv_color_t color = lv_color_hex(COLOR_MAIN);

	nfc_popup_close();
	strncpy(popup_path, path, sizeof(popup_path) - 1);
	popup_path[sizeof(popup_path) - 1] = '\0';

	// Rallume l'écran s'il était en veille
	LVGL_Screen_Wake();

	// Fond modal plein écran, au-dessus de l'écran actif
	popup_bg = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(popup_bg);
	lv_obj_set_size(popup_bg, LV_HOR_RES, LV_VER_RES);
	lv_obj_set_style_bg_color(popup_bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(popup_bg, LV_OPA_70, 0);
	lv_obj_add_flag(popup_bg, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(popup_bg, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(popup_bg, bg_event_cb, LV_EVENT_CLICKED, NULL);

	// Panneau : fond noir, contour seul
	lv_obj_t *panel = lv_obj_create(popup_bg);
	lv_obj_remove_style_all(panel);
	lv_obj_set_size(panel, POPUP_W, POPUP_H);
	lv_obj_center(panel);
	lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(panel, 8, 0);
	lv_obj_set_style_border_width(panel, 1, 0);
	lv_obj_set_style_border_color(panel, color, 0);
	lv_obj_set_style_pad_all(panel, 12, 0);
	lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE); // absorbe le clic
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title = lv_label_create(panel);
	lv_label_set_text(title, "Tag NFC");
	lv_obj_set_style_text_color(title, color, 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

	lv_obj_t *btn_play =
		popup_btn_create(panel, LV_SYMBOL_PLAY "\nJouer", play_btn_event_cb);
	lv_obj_align(btn_play, LV_ALIGN_BOTTOM_LEFT, 0, 0);

	lv_obj_t *btn_alarm =
		popup_btn_create(panel, LV_SYMBOL_BELL "\nWakeup", alarm_btn_event_cb);
	lv_obj_align(btn_alarm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void nfc_popup_tag_removed(void) {
	nfc_popup_close();
	if (music_playing) {
		Music_stop();
		music_playing = false;
	}
}
