#include "wakeup_settings.h"
#include "app_manager.h"
#include "lvgl.h"
#include "robot_cute.h"
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
static int hour = 7;
static int minute = 30;

static void update_time_label(void) {

	lv_label_set_text_fmt(label_time, "%02d:%02d", hour, minute);
}

static void btn_plus_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		alarm_t *alarm = getAlarm();
		alarm->minute++;
		if (alarm->minute > 59) {
			alarm->minute = 0;
			alarm->hour = (alarm->hour + 1) % 24;
		}
		update_time_label();
	}
}

static void btn_minus_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		alarm_t *alarm = getAlarm();
		alarm->minute--;
		if (alarm->minute < 0) {
			alarm->minute = 59;
			alarm->hour = (alarm->hour - 1 + 24) % 24;
		}
		update_time_label();
	}
}

static void btn_ok_event_handler(lv_event_t *event) {
	if (event->code == LV_EVENT_CLICKED) {
		//	app_manager_set_alarm()
		draw_robot();
	}
}

static void checkbox_event(lv_event_t *e) {
	lv_obj_t *cb = lv_event_get_target(e);

	if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
		bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);

		if (checked) {
			// Checkbox coché
		} else {
			// Checkbox décoché
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

		// ============================
		// Container Horaire
		// ============================
		lv_obj_t *time_container = lv_obj_create(alarm_screen);
		lv_obj_set_size(time_container, 220, 70);
		lv_obj_align(time_container, LV_ALIGN_TOP_MID, 0, 55);
		lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, 0);
		lv_obj_clear_flag(time_container, LV_OBJ_FLAG_SCROLLABLE);

		// Label HH:MM
		label_time = lv_label_create(time_container);
		lv_obj_add_style(label_time, &style_text, 0);
		update_time_label();
		lv_obj_align(label_time, LV_ALIGN_CENTER, 0, 15);

		// Bouton - (gauche)
		lv_obj_t *btn_minus = lv_btn_create(time_container);
		lv_obj_add_style(btn_minus, &style_btn, 0);
		lv_obj_set_size(btn_minus, 35, 35);
		lv_obj_align(btn_minus, LV_ALIGN_LEFT_MID, 10, 15);
		lv_obj_t *lbl_minus = lv_label_create(btn_minus);
		lv_label_set_text(lbl_minus, "-");
		lv_obj_center(lbl_minus);

		// Bouton + (droite)
		lv_obj_t *btn_plus = lv_btn_create(time_container);
		lv_obj_add_style(btn_plus, &style_btn, 0);
		lv_obj_set_size(btn_plus, 35, 35);
		lv_obj_align(btn_plus, LV_ALIGN_RIGHT_MID, -10, 15);
		lv_obj_t *lbl_plus = lv_label_create(btn_plus);
		lv_label_set_text(lbl_plus, "+");
		lv_obj_center(lbl_plus);

		// Cases à cocher (jours)
		lv_obj_t *day_container = lv_obj_create(alarm_screen);
		lv_obj_set_size(day_container, 220, 110); // Taille fixe
		lv_obj_align(day_container, LV_ALIGN_CENTER, 0, 40);
		lv_obj_set_style_bg_opa(day_container, LV_OPA_TRANSP, 0);

		// Désactiver le scroll
		lv_obj_clear_flag(day_container, LV_OBJ_FLAG_SCROLLABLE);

		int daysmask = 0x1F;
		for (int i = 0; i < 7; i++) {

			day_checks[i] = lv_checkbox_create(day_container);
			//  lv_obj_add_event_cb(cb, checkbox_event, LV_EVENT_VALUE_CHANGED,
			//  i);
			if ((daysmask & (1 + i)) == (i + 1)) {
				lv_obj_add_state(day_checks[i], LV_STATE_CHECKED);
			}
			lv_checkbox_set_text(day_checks[i], days[i]);
			lv_obj_add_style(day_checks[i], &style_text, LV_PART_MAIN);
			lv_obj_add_style(day_checks[i], &style_checkbox, LV_PART_INDICATOR);

			int col = i % 3; // 3 colonnes
			int row = i / 3; // 3 lignes
			lv_obj_align(day_checks[i], LV_ALIGN_TOP_LEFT, col * 70, row * 35);
		}

		// Bouton OK
		lv_obj_t *btn_ok = lv_btn_create(alarm_screen);
		lv_obj_add_style(btn_ok, &style_btn, 0);
		lv_obj_set_size(btn_ok, 140, 40);
		lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -10);

		lv_obj_t *lbl_ok = lv_label_create(btn_ok);
		lv_label_set_text(lbl_ok, "Valider");
		lv_obj_center(lbl_ok);

		// ============================
		// Callbacks + / -
		// ============================
		lv_obj_add_event_cb(btn_plus, btn_plus_event_handler, LV_EVENT_CLICKED,
							NULL);
		lv_obj_add_event_cb(btn_minus, btn_minus_event_handler,
							LV_EVENT_CLICKED, NULL);
		lv_obj_add_event_cb(btn_ok, btn_ok_event_handler, LV_EVENT_CLICKED,
							NULL);
	}
	lv_scr_load(alarm_screen);
}