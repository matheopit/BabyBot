#include "app_manager.h"
#include "lvgl.h"
#include "robot_cute.h"
#include "wakeup_settings.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define WAKEUP_SOUND_DIR "/sdcard"
#define MAX_SOUNDS 32

static lv_obj_t *sound_screen = NULL;
static lv_obj_t *sound_rows[MAX_SOUNDS];
static char sound_paths[MAX_SOUNDS][ALARM_SOUND_PATH_LEN];
static int sound_count = 0;

static lv_style_t style_bg;
static lv_style_t style_text;
static lv_style_t style_btn;
static lv_style_t style_row;
static lv_style_t style_row_selected;
static bool styles_ready = false;

static const char *path_basename(const char *path) {
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static bool has_mp3_extension(const char *fname) {
	const char *dot = strrchr(fname, '.');
	return dot != NULL && dot != fname && strcasecmp(dot, ".mp3") == 0;
}

// Liste les .mp3 présents dans WAKEUP_SOUND_DIR
static void scan_sounds(void) {
	sound_count = 0;

	DIR *dir = opendir(WAKEUP_SOUND_DIR);
	if (dir == NULL) {
		printf("wakeup_sound: impossible d'ouvrir %s\n", WAKEUP_SOUND_DIR);
		return;
	}

	struct dirent *entry;
	while (sound_count < MAX_SOUNDS && (entry = readdir(dir)) != NULL) {
		if (!has_mp3_extension(entry->d_name))
			continue;

		int written = snprintf(sound_paths[sound_count], ALARM_SOUND_PATH_LEN,
							   "%s/%s", WAKEUP_SOUND_DIR, entry->d_name);
		if (written < 0 || written >= ALARM_SOUND_PATH_LEN) {
			printf("wakeup_sound: chemin trop long, ignoré: %s\n",
				   entry->d_name);
			continue;
		}
		sound_count++;
	}
	closedir(dir);
}

// Met en évidence la ligne correspondant au son de l'alarme
static void update_selection(void) {
	const char *current = getAlarm()->sound;
	for (int i = 0; i < sound_count; i++) {
		if (strcmp(sound_paths[i], current) == 0) {
			lv_obj_add_state(sound_rows[i], LV_STATE_CHECKED);
		} else {
			lv_obj_clear_state(sound_rows[i], LV_STATE_CHECKED);
		}
	}
}

static void row_event_handler(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
		int idx = (int)(intptr_t)lv_event_get_user_data(e);
		alarm_t *alarm = getAlarm();
		strncpy(alarm->sound, sound_paths[idx], sizeof(alarm->sound) - 1);
		alarm->sound[sizeof(alarm->sound) - 1] = '\0';
		update_selection();
	}
}

static void btn_back_event_handler(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
		ui_alarm_screen_create();
	}
}

static void btn_ok_event_handler(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
		set_wakeup_config();
		draw_robot();
	}
}

// ======================================================
// STYLE INIT
// ======================================================
static void init_styles(void) {
	if (styles_ready)
		return;
	styles_ready = true;

	lv_style_init(&style_bg);
	lv_style_set_bg_color(&style_bg, lv_color_black());
	lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);

	lv_style_init(&style_text);
	lv_style_set_text_color(&style_text, lv_color_hex(COLOR_MAIN));

	lv_style_init(&style_btn);
	lv_style_set_bg_color(&style_btn, lv_color_hex(COLOR_MAIN));
	lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
	lv_style_set_radius(&style_btn, 8);

	lv_style_init(&style_row);
	lv_style_set_bg_opa(&style_row, LV_OPA_TRANSP);
	lv_style_set_border_side(&style_row, LV_BORDER_SIDE_BOTTOM);
	lv_style_set_border_width(&style_row, 1);
	lv_style_set_border_color(&style_row, lv_color_hex(0x224a63));
	lv_style_set_text_color(&style_row, lv_color_hex(COLOR_MAIN));
	lv_style_set_pad_hor(&style_row, 6);

	lv_style_init(&style_row_selected);
	lv_style_set_bg_color(&style_row_selected, lv_color_hex(COLOR_MAIN));
	lv_style_set_bg_opa(&style_row_selected, LV_OPA_COVER);
	lv_style_set_text_color(&style_row_selected, lv_color_black());
}

// ======================================================
// SCREEN CREATION
// ======================================================

void ui_alarm_sound_screen_create(void) {
	init_styles();

	// Recréé à chaque ouverture pour refléter le contenu actuel de la carte SD
	if (sound_screen != NULL) {
		lv_obj_del(sound_screen);
		sound_screen = NULL;
	}

	scan_sounds();

	sound_screen = lv_obj_create(NULL);
	lv_obj_add_style(sound_screen, &style_bg, 0);
	lv_obj_set_size(sound_screen, 240, 320);
	lv_obj_clear_flag(sound_screen, LV_OBJ_FLAG_SCROLLABLE);

	// Titre
	lv_obj_t *label_title = lv_label_create(sound_screen);
	lv_obj_add_style(label_title, &style_text, 0);
	lv_label_set_text(label_title, "Son du reveil");
	lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);

	// Liste des fichiers
	lv_obj_t *list = lv_obj_create(sound_screen);
	lv_obj_set_size(list, 220, 220);
	lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 35);
	lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(list, 4, 0);
	lv_obj_set_style_pad_row(list, 0, 0);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);

	if (sound_count == 0) {
		lv_obj_t *empty_lbl = lv_label_create(list);
		lv_obj_add_style(empty_lbl, &style_text, 0);
		lv_label_set_text(empty_lbl,
						  "Aucun fichier .mp3 sur\n" WAKEUP_SOUND_DIR);
		lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
	}

	for (int i = 0; i < sound_count; i++) {
		lv_obj_t *row = lv_obj_create(list);
		lv_obj_remove_style_all(row);
		lv_obj_add_style(row, &style_row, 0);
		lv_obj_add_style(row, &style_row_selected, LV_STATE_CHECKED);
		lv_obj_set_size(row, lv_pct(100), 34);
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_event_cb(row, row_event_handler, LV_EVENT_CLICKED,
							(void *)(intptr_t)i);

		lv_obj_t *icon = lv_label_create(row);
		lv_label_set_text(icon, LV_SYMBOL_AUDIO);
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

		lv_obj_t *name_lbl = lv_label_create(row);
		lv_label_set_text(name_lbl, path_basename(sound_paths[i]));
		lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
		lv_obj_set_width(name_lbl, lv_pct(85));
		lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 24, 0);

		sound_rows[i] = row;
	}
	update_selection();

	// Bouton Retour
	lv_obj_t *btn_back = lv_btn_create(sound_screen);
	lv_obj_add_style(btn_back, &style_btn, 0);
	lv_obj_set_size(btn_back, 100, 40);
	lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
	lv_obj_add_event_cb(btn_back, btn_back_event_handler, LV_EVENT_CLICKED,
						NULL);

	lv_obj_t *lbl_back = lv_label_create(btn_back);
	lv_label_set_text(lbl_back, "Retour");
	lv_obj_center(lbl_back);

	// Bouton Valider
	lv_obj_t *btn_ok = lv_btn_create(sound_screen);
	lv_obj_add_style(btn_ok, &style_btn, 0);
	lv_obj_set_size(btn_ok, 100, 40);
	lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
	lv_obj_add_event_cb(btn_ok, btn_ok_event_handler, LV_EVENT_CLICKED, NULL);

	lv_obj_t *lbl_ok = lv_label_create(btn_ok);
	lv_label_set_text(lbl_ok, "Valider");
	lv_obj_center(lbl_ok);

	lv_scr_load(sound_screen);
}
