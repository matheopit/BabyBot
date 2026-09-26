#include "wakeup_settings.h"
#include "app_manager.h"
#include "lvgl.h"
#include "robot_cute.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *alarm_screen = NULL;
static lv_obj_t *toggle_alarm;
static lv_obj_t *day_checks[7];

static const char *days[7] = {"Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"};

static lv_style_t style_bg;
static lv_style_t style_text;
static lv_style_t style_btn;
static lv_style_t style_checkbox;

static lv_obj_t *label_time;

// Dialog de reglage de l'heure (2 rollers heures / minutes)
static lv_obj_t *time_modal_bg = NULL;
static lv_obj_t *roller_hour;
static lv_obj_t *roller_min;
static char roller_hour_opts[24 * 3];
static char roller_min_opts[60 * 3];

static void update_time_label(void) {
	alarm_t *alarm = getAlarm();
	lv_label_set_text_fmt(label_time, "%02d:%02d", alarm->hour, alarm->minute);
}

// Remplit une liste d'options "00\n01\n...", pour lv_roller
static void build_roller_opts(char *buf, int count) {
	char *p = buf;
	for (int i = 0; i < count; i++) {
		p += sprintf(p, i < count - 1 ? "%02d\n" : "%02d", i);
	}
}

static void time_modal_close(void) {
	if (time_modal_bg != NULL) {
		lv_obj_del(time_modal_bg);
		time_modal_bg = NULL;
	}
}

static void time_ok_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		alarm_t *alarm = getAlarm();
		alarm->hour = lv_roller_get_selected(roller_hour);
		alarm->minute = lv_roller_get_selected(roller_min);
		update_time_label();
		time_modal_close();
	}
}

static void time_cancel_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		time_modal_close();
	}
}

static lv_obj_t *time_roller_create(lv_obj_t *parent, const char *opts,
									int selected) {
	lv_obj_t *roller = lv_roller_create(parent);
	// Mode NORMAL : en mode INFINITE, LVGL duplique la liste 7 fois et la
	// hauteur du label (60 x 7 lignes en police 32) depasse la limite des
	// coordonnees LVGL -> la liste des minutes ne s'affiche plus.
	lv_roller_set_options(roller, opts, LV_ROLLER_MODE_NORMAL);
	lv_roller_set_visible_row_count(roller, 3);
	lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
	lv_obj_set_width(roller, 70);
	// Meme police pour la ligne selectionnee, sinon le rectangle bleu est
	// dimensionne pour la police par defaut et le texte est decale
	lv_obj_set_style_text_font(roller, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_font(roller, &lv_font_montserrat_32,
							   LV_PART_SELECTED);
	lv_obj_set_style_bg_color(roller, lv_color_black(), 0);
	lv_obj_set_style_text_color(roller, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_border_color(roller, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_bg_color(roller, lv_color_hex(COLOR_MAIN),
							  LV_PART_SELECTED);
	lv_obj_set_style_text_color(roller, lv_color_black(), LV_PART_SELECTED);
	return roller;
}

static lv_obj_t *time_dialog_btn_create(lv_obj_t *parent, const char *text,
										lv_event_cb_t cb) {
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_add_style(btn, &style_btn, 0);
	lv_obj_set_size(btn, 90, 36);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *lbl = lv_label_create(btn);
	lv_label_set_text(lbl, text);
	lv_obj_center(lbl);
	return btn;
}

// Ouvre le dialog modal de reglage de l'heure du reveil
static void time_modal_open(void) {
	if (time_modal_bg != NULL)
		return;
	alarm_t *alarm = getAlarm();

	// Fond semi-transparent qui bloque les clics sur l'ecran dessous
	time_modal_bg = lv_obj_create(alarm_screen);
	lv_obj_remove_style_all(time_modal_bg);
	lv_obj_set_size(time_modal_bg, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(time_modal_bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(time_modal_bg, LV_OPA_70, 0);
	lv_obj_add_flag(time_modal_bg, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(time_modal_bg, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *panel = lv_obj_create(time_modal_bg);
	lv_obj_set_size(panel, 220, 250);
	lv_obj_center(panel);
	lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_border_width(panel, 2, 0);
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title = lv_label_create(panel);
	lv_obj_add_style(title, &style_text, 0);
	lv_label_set_text(title, "Heure du reveil");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, -4);

	roller_hour = time_roller_create(panel, roller_hour_opts, alarm->hour);
	lv_obj_align(roller_hour, LV_ALIGN_CENTER, -45, -8);

	lv_obj_t *colon = lv_label_create(panel);
	lv_obj_add_style(colon, &style_text, 0);
	lv_obj_set_style_text_font(colon, &lv_font_montserrat_32, 0);
	lv_label_set_text(colon, ":");
	lv_obj_align(colon, LV_ALIGN_CENTER, 0, -8);

	roller_min = time_roller_create(panel, roller_min_opts, alarm->minute);
	lv_obj_align(roller_min, LV_ALIGN_CENTER, 45, -8);

	lv_obj_t *btn_cancel =
		time_dialog_btn_create(panel, "Annuler", time_cancel_event_handler);
	lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 4);
	lv_obj_t *btn_ok =
		time_dialog_btn_create(panel, "OK", time_ok_event_handler);
	lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 4);
}

static void label_time_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		time_modal_open();
	}
}

static void btn_next_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		ui_alarm_sound_screen_create();
	}
}

static void checkbox_event(lv_event_t *e) {
	lv_obj_t *cb = lv_event_get_target(e);

	if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
		int day = (int)(intptr_t)lv_event_get_user_data(e);
		alarm_t *alarm = getAlarm();

		if (lv_obj_has_state(cb, LV_STATE_CHECKED)) {
			alarm->days |= (1 << day);
		} else {
			alarm->days &= ~(1 << day);
		}
	}
}

static void toggle_event(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
		lv_obj_t *sw = lv_event_get_target(e);
		getAlarm()->enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
	}
}

static void update_toggle(void) {
	if (getAlarm()->enabled) {
		lv_obj_add_state(toggle_alarm, LV_STATE_CHECKED);
	} else {
		lv_obj_clear_state(toggle_alarm, LV_STATE_CHECKED);
	}
}

// Coche les jours selon le bitmask de l'alarme (bit 0 = lundi)
static void update_day_checks(void) {
	int daysmask = getAlarm()->days;
	for (int i = 0; i < 7; i++) {
		if (daysmask & (1 << i)) {
			lv_obj_add_state(day_checks[i], LV_STATE_CHECKED);
		} else {
			lv_obj_clear_state(day_checks[i], LV_STATE_CHECKED);
		}
	}
}

// ======================================================
// STYLE INIT
// ======================================================
static void init_styles(void) {
	lv_style_init(&style_bg);
	lv_style_set_bg_color(&style_bg, lv_color_black());
	lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);

	lv_style_init(&style_text);
	lv_style_set_text_color(&style_text, lv_color_hex(COLOR_MAIN));

	lv_style_init(&style_btn);
	lv_style_set_bg_color(&style_btn, lv_color_hex(COLOR_MAIN));
	lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
	lv_style_set_radius(&style_btn, 8);

	lv_style_init(&style_checkbox);
	lv_style_set_bg_color(&style_checkbox, lv_color_hex(COLOR_MAIN));
	lv_style_set_border_color(&style_checkbox, lv_color_hex(COLOR_MAIN));
	lv_style_set_border_width(&style_checkbox, 2);
}

// ======================================================
// SCREEN CREATION
// ======================================================

void ui_alarm_screen_create(void) {
	if (alarm_screen == NULL) {
		init_styles();
		build_roller_opts(roller_hour_opts, 24);
		build_roller_opts(roller_min_opts, 60);

		alarm_screen = lv_obj_create(NULL);
		lv_obj_add_style(alarm_screen, &style_bg, 0);
		lv_obj_set_size(alarm_screen, 240, 320);

		// Titre
		lv_obj_t *label_title = lv_label_create(alarm_screen);
		lv_obj_add_style(label_title, &style_text, 0);
		lv_label_set_text(label_title, "Activer");
		lv_obj_align(label_title, LV_ALIGN_TOP_LEFT, 10, 10);

		// Toggle ON/OFF
		toggle_alarm = lv_switch_create(alarm_screen);
		lv_obj_align(toggle_alarm, LV_ALIGN_TOP_RIGHT, -10, 10);
		lv_obj_set_style_bg_color(toggle_alarm, lv_color_hex(0x4DA6FF),
								  LV_PART_INDICATOR);
		lv_obj_add_event_cb(toggle_alarm, toggle_event, LV_EVENT_VALUE_CHANGED,
							NULL);

		// ============================
		// Container Horaire
		// ============================
		lv_obj_t *time_container = lv_obj_create(alarm_screen);
		lv_obj_set_size(time_container, 220, 70);
		lv_obj_align(time_container, LV_ALIGN_TOP_MID, 0, 55);
		lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, 0);
		lv_obj_clear_flag(time_container, LV_OBJ_FLAG_SCROLLABLE);

		// Label HH:MM : occupe toute la zone, clic -> dialog de reglage
		label_time = lv_label_create(time_container);
		lv_obj_add_style(label_time, &style_text, 0);
		lv_obj_set_style_text_font(label_time, &lv_font_montserrat_32, 0);
		lv_obj_set_style_text_align(label_time, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_width(label_time, LV_PCT(100));
		update_time_label();
		lv_obj_center(label_time);
		lv_obj_add_flag(label_time, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(label_time, label_time_event_handler,
							LV_EVENT_CLICKED, NULL);

		// Cases à cocher (jours)
		lv_obj_t *day_container = lv_obj_create(alarm_screen);
		lv_obj_set_size(day_container, 220, 110); // Taille fixe
		lv_obj_align(day_container, LV_ALIGN_CENTER, 0, 40);
		lv_obj_set_style_bg_opa(day_container, LV_OPA_TRANSP, 0);

		// Désactiver le scroll
		lv_obj_clear_flag(day_container, LV_OBJ_FLAG_SCROLLABLE);

		for (int i = 0; i < 7; i++) {

			day_checks[i] = lv_checkbox_create(day_container);
			lv_obj_add_event_cb(day_checks[i], checkbox_event,
								LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
			lv_checkbox_set_text(day_checks[i], days[i]);
			lv_obj_add_style(day_checks[i], &style_text, LV_PART_MAIN);
			lv_obj_add_style(day_checks[i], &style_checkbox, LV_PART_INDICATOR);

			int col = i % 3; // 3 colonnes
			int row = i / 3; // 3 lignes
			lv_obj_align(day_checks[i], LV_ALIGN_TOP_LEFT, col * 70, row * 35);
		}

		// Bouton Suivant
		lv_obj_t *btn_next = lv_btn_create(alarm_screen);
		lv_obj_add_style(btn_next, &style_btn, 0);
		lv_obj_set_size(btn_next, 140, 40);
		lv_obj_align(btn_next, LV_ALIGN_BOTTOM_MID, 0, -10);

		lv_obj_t *lbl_next = lv_label_create(btn_next);
		lv_label_set_text(lbl_next, "Suivant");
		lv_obj_center(lbl_next);

		lv_obj_add_event_cb(btn_next, btn_next_event_handler, LV_EVENT_CLICKED,
							NULL);
	}
	time_modal_close();
	update_toggle();
	update_time_label();
	update_day_checks();
	getAlarm()->in_settings = true;
	lv_scr_load(alarm_screen);
}