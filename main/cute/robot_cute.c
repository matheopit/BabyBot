#include "extra/layouts/flex/lv_flex.h"
#include "hal/lv_hal_disp.h"
#include "lvgl.h"

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

		/* Mains et zone de clic : créées une seule fois */
		create_robot_hand(robot_screen);
		create_full_click_zone(robot_screen);
	} else {
		robot_refresh_eyes();
	}
	lv_scr_load(robot_screen);
}
