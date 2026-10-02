#include "extra/layouts/flex/lv_flex.h"
#include "hal/lv_hal_disp.h"
#include "lvgl.h"

#include "BAT_Driver.h"
#include "PCM5101.h"
#include "app_manager.h"
#include "draw_function.h"
#include "robot_face.h"
#include "esp_log.h"
/* --- Animation du regard --- */
typedef struct {
	lv_obj_t *canvas;
	lv_obj_t *left_eye;
	lv_obj_t *right_eye;
} face_t;
static const char *TAG = "ROBOT_CUTE";
static face_t face;

/* Animation callback : change la hauteur de l'œil */
static void eye_anim_cb(void *obj, int32_t v) {
	lv_obj_set_height(face.left_eye, v);
	lv_obj_set_height(face.right_eye, v);
}

static lv_obj_t *hand_right;
static lv_obj_t *hand_left;

/* Supprime les deux mains (libère aussi leurs animations) */
static void hands_delete_timer_cb(lv_timer_t *timer) {
	if (hand_left) {
		lv_obj_del(hand_left);
		hand_left = NULL;
	}
	if (hand_right) {
		lv_obj_del(hand_right);
		hand_right = NULL;
	}
}

static lv_obj_t *create_hand(lv_obj_t *parent, int posx, int posy) {

	lv_obj_t *container = lv_obj_create(parent);
	lv_obj_set_size(container, 40, 50);
	// Invisible mais actif : fond noir opaque (comme l'écran) plutôt que
	// transparent, sinon LVGL ne peut pas dessiner la rotation sans
	// LV_COLOR_SCREEN_TRANSP
	lv_obj_set_style_bg_color(container, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(container, 0, 0);
	lv_obj_set_style_border_width(container, 0, 0);
	lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);

	// Enlever tout padding interne
	lv_obj_set_style_pad_all(container, 0, 0);
	lv_obj_set_pos(container, posx, posy);

	lv_obj_t *hand = lv_obj_create(container);

	lv_obj_set_size(hand, 40, 30);		  // paume
	lv_obj_set_style_radius(hand, 10, 0); // arrondi
	lv_obj_set_style_bg_color(hand, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_bg_opa(hand, LV_OPA_COVER, 0);
	lv_obj_set_pos(hand, 0, 20);

	// doigts gauche (3 petits rectangles)
	for (int i = 0; i < 3; i++) {
		lv_obj_t *finger = lv_obj_create(container);
		lv_obj_set_size(finger, 8, 18);
		lv_obj_set_style_radius(finger, 4, 0);
		lv_obj_set_style_bg_color(finger, lv_color_hex(COLOR_MAIN), 0);
		lv_obj_set_style_bg_opa(finger, LV_OPA_COVER, 0);
		lv_obj_set_pos(finger,
					   +6 + i * 10, // x
					   2);			// juste au-dessus de la paume
	}
	return container;
}

static void hand_right_rotate_anim_cb(void *obj, int32_t v) {
	lv_obj_set_style_transform_angle(obj, v, 0);
}

void create_robot_hand(lv_obj_t *parent) {
	

	// Main gauche
	hand_left = create_hand(parent, 40, 200);
	// Main droite
	hand_right = create_hand(parent, 240 - 40 - 40, 200);

	/* Les mains disparaissent au bout de 10 secondes */
	lv_timer_t *t = lv_timer_create(hands_delete_timer_cb, 10000, NULL);
	lv_timer_set_repeat_count(t, 1);
	/* Rotation gauche → droite de 45° autour du poignet (bas de la main) */
	lv_obj_set_style_transform_pivot_x(hand_right, 20, 0);
	lv_obj_set_style_transform_pivot_y(hand_right, 50, 0);

	lv_anim_t r;
	lv_anim_init(&r);

	lv_anim_set_var(&r, hand_right);
	lv_anim_set_exec_cb(&r, hand_right_rotate_anim_cb);

	lv_anim_set_time(&r, 1000);
	lv_anim_set_playback_time(&r, 1000);
	lv_anim_set_repeat_count(&r, LV_ANIM_REPEAT_INFINITE);

	lv_anim_set_values(&r, -450, 450); // en 0.1° : -45° → +45°
	lv_anim_start(&r);
}


/* Crée les deux yeux (face.left_eye / face.right_eye) selon l'humeur */
static void create_eyes(lv_obj_t *parent) {
	const lv_coord_t y = 20, w = 80, h = 80;
	const lv_coord_t left_x = 30, right_x = 130;
	mood_t m = app_manager_get_mood();
	ESP_LOGI(TAG, "Humeur  : %d", m);
	switch (m) {
	case MOOD_HAPPY:
		face.left_eye = create_square_eye_happy(parent, left_x, y, w, h);
		face.right_eye = create_square_eye_happy(parent, right_x, y, w, h);
		break;
	case MOOD_SAD:
		face.left_eye = create_square_eye_sad(parent, left_x, y, w, h, EYE_LEFT);
		face.right_eye =
			create_square_eye_sad(parent, right_x, y, w, h, EYE_RIGHT);
		break;
	case MOOD_ANGRY:
		face.left_eye =
			create_square_eye_angry(parent, left_x, y, w, h, EYE_LEFT);
		face.right_eye =
			create_square_eye_angry(parent, right_x, y, w, h, EYE_RIGHT);
		break;
	case MOOD_TIRED:
		face.left_eye = create_square_eye_tired(parent, left_x, y, w, h);
		face.right_eye = create_square_eye_tired(parent, right_x, y, w, h);
		break;
 default:
		face.left_eye = create_square_eye(parent, left_x, y, w, h);
		face.right_eye = create_square_eye(parent, right_x, y, w, h);
		break;
	};
}

/* Crée les deux yeux + clignement */
static void create_robot_square_eyes_with_blink(lv_obj_t *parent) {
	/* Yeux selon l'humeur */
	create_eyes(parent);
	/* Sourire */
//	draw_robot_mouth(parent);

	/* Animation de clignement */
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_time(&a, 120);			// fermeture rapide
	lv_anim_set_playback_time(&a, 120); // ouverture rapide
	lv_anim_set_repeat_delay(&a, 1800); // temps entre clignements
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_exec_cb(&a, eye_anim_cb);
	/* Lance l’animation pour les deux yeux */
	lv_anim_set_var(&a, &face);
	lv_anim_set_values(&a, 80, 0);
	lv_anim_start(&a);
}

static lv_obj_t *robot_screen = NULL;
/* Calque des yeux : seul élément reconstruit quand l'humeur change */
static lv_obj_t *eyes_layer = NULL;
static mood_t eyes_mood;

/* Reconstruit les yeux si l'humeur a changé depuis leur création */
static void robot_refresh_eyes(void) {
	mood_t m = app_manager_get_mood();
	if (m == eyes_mood)
		return;
	/* Stoppe le clignement avant de supprimer les yeux qu'il anime */
	lv_anim_del(&face, eye_anim_cb);
	lv_obj_clean(eyes_layer);
	eyes_mood = m;
	create_robot_square_eyes_with_blink(eyes_layer);
}

/* À appeler uniquement depuis la tâche LVGL (boucle principale) */
void robot_update_mood(void) {
	if (robot_screen)
		robot_refresh_eyes();
}

/* Bouton « Arrêter le réveil » (police sans accents), au milieu de l'écran pendant la sonnerie */
static lv_obj_t *alarm_btn = NULL;

static void alarm_btn_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	app_manager_alarm_stop();
}

static void alarm_btn_create(void) {
	lv_color_t color = lv_color_hex(COLOR_MAIN);

	alarm_btn = lv_btn_create(robot_screen);
	lv_obj_remove_style_all(alarm_btn);
	lv_obj_set_size(alarm_btn, 180, 70);
	lv_obj_align(alarm_btn, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_radius(alarm_btn, 8, 0);
	lv_obj_set_style_border_width(alarm_btn, 2, 0);
	lv_obj_set_style_border_color(alarm_btn, color, 0);
	lv_obj_set_style_bg_color(alarm_btn, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(alarm_btn, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(alarm_btn, color, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(alarm_btn, LV_OPA_40, LV_STATE_PRESSED);
	lv_obj_add_event_cb(alarm_btn, alarm_btn_event_cb, LV_EVENT_CLICKED,
						NULL);

	lv_obj_t *lbl = lv_label_create(alarm_btn);
	lv_label_set_text(lbl, LV_SYMBOL_BELL "\nArreter le reveil");
	lv_obj_set_style_text_color(lbl, color, 0);
	lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_center(lbl);
}

void robot_alarm_update(void) {
	if (!robot_screen)
		return;
	bool ringing = app_manager_alarm_ringing();
	if (ringing && !alarm_btn) {
		alarm_btn_create();
	} else if (!ringing && alarm_btn) {
		lv_obj_del(alarm_btn);
		alarm_btn = NULL;
	}
}

/* --- Notes de musique qui défilent pendant la lecture --- */
#define NOTE_MAX 8			/* nombre max de notes affichées en même temps */
#define NOTE_END_Y (-40)	/* la note disparaît au-dessus de l'écran */

/* Calque transparent des notes : au-dessus des yeux, sous les mains */
static lv_obj_t *notes_layer = NULL;

/* Fait monter la note en oscillant, puis la fait disparaître en fondu
 * v : progression de 0 à 1000 */
static void note_anim_cb(void *var, int32_t v) {
	lv_obj_t *note = var;
	int32_t start_y = lv_disp_get_ver_res(NULL);
	int32_t base_x = (int32_t)(intptr_t)lv_obj_get_user_data(note);
	/* Oscillation gauche/droite de ±12 px (2 périodes sur le trajet) */
	int32_t sway = (lv_trigo_sin((int16_t)((v * 720 / 1000) % 360)) * 12) / 32768;
	int32_t y = start_y - v * (start_y - NOTE_END_Y) / 1000;

	lv_obj_set_pos(note, base_x + sway, y);
	/* Pleine opacité sur les 70 premiers %, fondu sur le reste */
	lv_opa_t opa = (v < 700) ? LV_OPA_COVER : (lv_opa_t)((1000 - v) * 255 / 300);
	lv_obj_set_style_text_opa(note, opa, 0);
}

/* Fin du trajet : supprime la note (en différé, on est dans son animation) */
static void note_anim_ready_cb(lv_anim_t *a) { lv_obj_del_async(a->var); }

/* Crée une note à une position, une taille et une couleur aléatoires */
static void note_spawn(void) {
	static const uint32_t colors[] = {COLOR_MAIN, 0xFFD24D, 0xFF7AC8, 0x7CFFB2,
									  0xFFFFFF};
	lv_obj_t *note = lv_label_create(notes_layer);
	bool big = lv_rand(0, 1);

	lv_label_set_text(note, LV_SYMBOL_AUDIO);
	lv_obj_set_style_text_font(
		note, big ? &lv_font_montserrat_32 : &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(
		note, lv_color_hex(colors[lv_rand(0, sizeof(colors) / sizeof(colors[0]) - 1)]),
		0);
	/* Abscisse de base mémorisée pour l'oscillation (marge pour ±12 px) */
	int32_t max_x = lv_disp_get_hor_res(NULL) - 50;
	lv_obj_set_user_data(note, (void *)(intptr_t)lv_rand(15, max_x));

	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, note);
	lv_anim_set_exec_cb(&a, note_anim_cb);
	lv_anim_set_ready_cb(&a, note_anim_ready_cb);
	lv_anim_set_values(&a, 0, 1000);
	lv_anim_set_time(&a, lv_rand(3000, 5000));
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_start(&a);
	/* Position initiale (sous l'écran) avant le premier pas d'animation */
	note_anim_cb(note, 0);
}

/* Toutes les 250 à 700 ms (durée tirée au hasard), fait apparaître une note
 * si l'écran robot est affiché et qu'une musique est en cours de lecture */
static void notes_timer_cb(lv_timer_t *timer) {
	lv_timer_set_period(timer, lv_rand(250, 700));
	if (!robot_screen || lv_scr_act() != robot_screen)
		return;
	if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING)
		return;
	if (lv_obj_get_child_cnt(notes_layer) >= NOTE_MAX)
		return;
	note_spawn();
}

/* --- Indicateur de batterie (en bas à droite) --- */
#define BAT_REFRESH_MS 5000 /* rafraîchissement du pourcentage */
#define COLOR_BAT_LOW 0xFFA500
#define COLOR_BAT_CRITICAL 0xFF3030
/* Sous ce pourcentage : icône affichée et yeux fatigués. Il faut remonter
 * au-dessus de BAT_TIRED_PERCENT + BAT_TIRED_HYST pour en sortir (évite de
 * basculer en boucle autour du seuil) */
#define BAT_TIRED_PERCENT 20
#define BAT_TIRED_HYST 5

static lv_obj_t *bat_label = NULL;
static bool bat_low = false;

/* Met à jour l'indicateur (affiché seulement batterie basse) et l'humeur */
void robot_battery_update(void) {
	if (!bat_label)
		return;
	uint8_t pct = BAT_Get_Percent();
	if (!BAT_Is_Valid())
		bat_low = false;
	else if (bat_low)
		bat_low = pct <= BAT_TIRED_PERCENT + BAT_TIRED_HYST;
	else
		bat_low = pct <= BAT_TIRED_PERCENT;
	app_manager_set_battery_low(bat_low);

	if (!bat_low) {
		lv_obj_add_flag(bat_label, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(bat_label, LV_OBJ_FLAG_HIDDEN);
	const char *icon = pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
	uint32_t color;
	switch (BAT_Get_State()) {
	case BAT_STATE_CRITICAL:
		color = COLOR_BAT_CRITICAL;
		break;
	case BAT_STATE_LOW:
		color = COLOR_BAT_LOW;
		break;
	default:
		color = COLOR_MAIN;
		break;
	}
	lv_label_set_text_fmt(bat_label, "%s %u%%", icon, (unsigned)pct);
	lv_obj_set_style_text_color(bat_label, lv_color_hex(color), 0);
}

static void bat_timer_cb(lv_timer_t *timer) { robot_battery_update(); }

static void bat_indicator_create(lv_obj_t *parent) {
	bat_label = lv_label_create(parent);
	lv_obj_clear_flag(bat_label, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_align(bat_label, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
	robot_battery_update();
	lv_timer_create(bat_timer_cb, BAT_REFRESH_MS, NULL);
}

void draw_robot() {

	if (!robot_screen) {

		robot_screen = lv_obj_create(NULL);
		/* Fond écran noir */
		lv_obj_set_style_bg_color(robot_screen, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(robot_screen, LV_OPA_COVER, 0);

		/* Calque transparent plein écran, créé en premier pour rester sous
		 * les mains et la zone de clic */
		eyes_layer = lv_obj_create(robot_screen);
		lv_obj_remove_style_all(eyes_layer);
		lv_obj_set_size(eyes_layer, LV_PCT(100), LV_PCT(100));
		lv_obj_clear_flag(eyes_layer,
						  LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
		eyes_mood = app_manager_get_mood();
		create_robot_square_eyes_with_blink(eyes_layer);

		/* Calque des notes de musique (sous les mains et la zone de clic) */
		notes_layer = lv_obj_create(robot_screen);
		lv_obj_remove_style_all(notes_layer);
		lv_obj_set_size(notes_layer, LV_PCT(100), LV_PCT(100));
		lv_obj_clear_flag(notes_layer,
						  LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
		lv_timer_create(notes_timer_cb, 500, NULL);

		/* Indicateur de batterie (sous la zone de clic) */
		bat_indicator_create(robot_screen);

		/* Mains et zone de clic : créées une seule fois */
		create_robot_hand(robot_screen);
		create_full_click_zone(robot_screen);
	} else {
		robot_refresh_eyes();
	}
	/* Créé après la zone de clic pour être au-dessus */
	robot_alarm_update();
	lv_scr_load(robot_screen);
}
