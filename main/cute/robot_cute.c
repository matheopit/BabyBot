#include "extra/layouts/flex/lv_flex.h"
#include "hal/lv_hal_disp.h"
#include "lvgl.h"

#define CANVAS_W 240
#define CANVAS_H 120

#include "app_manager.h"
#include "draw_function.h"
/* Dessine un disque plein */
static void draw_circle(lv_obj_t *canvas, lv_coord_t cx, lv_coord_t cy,
						lv_coord_t r, lv_color_t color) {
	lv_draw_arc_dsc_t arc;
	lv_draw_arc_dsc_init(&arc);

	arc.color = color;
	arc.width = r * 2;
	arc.rounded = 1;

	lv_canvas_draw_arc(canvas, cx, cy, r, 0, 360, &arc);
}

/* Dessine un sourcil incliné */
static void draw_eyebrow(lv_obj_t *canvas, lv_coord_t x1, lv_coord_t y1,
						 lv_coord_t x2, lv_coord_t y2, lv_color_t color) {
	lv_draw_line_dsc_t line;
	lv_draw_line_dsc_init(&line);
	line.color = color;
	line.width = 6;

	lv_point_t pts[2] = {{x1, y1}, {x2, y2}};
	lv_canvas_draw_line(canvas, pts, 2, &line);
}

/* Dessine un œil en colère */
static void draw_angry_eye(lv_obj_t *canvas, lv_coord_t cx, lv_coord_t cy,
						   lv_coord_t r) {
	lv_color_t red = lv_color_hex(0xFF3B30);
	lv_color_t black = lv_color_black();
	lv_color_t white = lv_color_white();

	/* Iris rouge */
	draw_circle(canvas, cx, cy, r, red);

	/* Pupille verticale (colère) */
	lv_draw_rect_dsc_t pupil;
	lv_draw_rect_dsc_init(&pupil);
	pupil.bg_color = black;
	pupil.radius = LV_RADIUS_CIRCLE;

	lv_canvas_draw_rect(canvas, cx - r / 6, cy - r / 2, r / 3, r, &pupil);

	/* Reflet blanc */
	draw_circle(canvas, cx - r / 3, cy - r / 3, r / 6, white);

	/* Sourcil incliné (colère) */
	draw_eyebrow(canvas, cx - r, cy - r, // début
				 cx + r / 2, cy - r / 2, // fin
				 black);
}

/* Fonction principale */
void create_angry_eyes(lv_obj_t *parent) {
	lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

	lv_obj_t *canvas = lv_canvas_create(parent);
	lv_obj_set_size(canvas, CANVAS_W, CANVAS_H);
	lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);

	static lv_color_t buf[CANVAS_W * CANVAS_H];
	lv_canvas_set_buffer(canvas, buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);

	lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

	/* Œil gauche */
	draw_angry_eye(canvas, 70, 60, 30);

	/* Œil droit */
	draw_angry_eye(canvas, 170, 60, 30);
}

void draw_robot_mouth(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy,
					  lv_coord_t r) {

	/* Création du canvas */
	lv_obj_t *canvas = lv_canvas_create(parent);
	lv_obj_set_size(canvas, CANVAS_W, CANVAS_H);

	/* Buffer pour l’image */
	static lv_color_t buf[CANVAS_W * CANVAS_H];
	lv_canvas_set_buffer(canvas, buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);

	lv_obj_set_style_bg_color(canvas, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
	/*
	lv_draw_arc_dsc_t arc_dsc;
	lv_draw_arc_dsc_init(&arc_dsc);


	arc_dsc.color = lv_color_hex(0x4DA6FF);   // même bleu mignon que les yeux
	arc_dsc.width = 60;                        // épaisseur du trait
	arc_dsc.rounded = 1;

	// Arc de 200° à 340° → petit sourire
	lv_canvas_draw_arc(canvas, cx, cy, r, 200, 340, &arc_dsc);

	*/
	lv_draw_arc_dsc_t dsc;
	lv_draw_arc_dsc_init(&dsc);

	dsc.color = lv_color_hex(0x4DA6FF);
	dsc.width = 2; // largeur de l’arc

	lv_canvas_draw_arc(canvas, CANVAS_W / 2, CANVAS_H / 2, 15, 00, 360, &dsc);
}

/* --- Animation du regard --- */
typedef struct {
	lv_obj_t *canvas;
	lv_obj_t *left_eye;
	lv_obj_t *right_eye;
} face_t;

static face_t face;

/* Animation callback : change la hauteur de l'œil */
static void eye_anim_cb(void *obj, int32_t v) {
	lv_obj_set_height(face.left_eye, v);
	lv_obj_set_height(face.right_eye, v);
}

/* Crée un œil carré arrondi bleu */
static lv_obj_t *create_square_eye(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								   lv_coord_t w, lv_coord_t h) {
	lv_obj_t *eye = lv_obj_create(parent);
	lv_obj_set_size(eye, w, h);
	lv_obj_set_style_bg_color(eye, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_radius(eye, w / 4, 0);
	lv_obj_set_style_border_width(eye, 0, 0);
	lv_obj_set_style_pad_all(eye, 0, 0);

	lv_obj_set_pos(eye, x, y);

	return eye;
}

lv_obj_t *hand_right;

static void hand_right_anim_cb(void *obj, int32_t v) {
	lv_obj_set_x(obj, v);
}

void animate_robot_hand_right(void) {
	lv_anim_t a;
	lv_anim_init(&a);

	lv_anim_set_var(&a, hand_right);
	lv_anim_set_exec_cb(&a, hand_right_anim_cb);

	lv_anim_set_time(&a, 1000);
	lv_anim_set_playback_time(&a, 1000);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);

	lv_anim_set_values(&a, 160, 200); // ← ça marche, valeurs en pixels
	lv_anim_start(&a);
}

static lv_obj_t *create_hand(lv_obj_t *parent, int posx, int posy) {

	lv_obj_t *container = lv_obj_create(parent);
	lv_obj_set_size(container, 40, 50);
	// Invisible mais actif
	lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
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
					   2);		// juste au-dessus de la paume
	}
	return container;
}

/* Crée les deux yeux + clignement */
void create_robot_square_eyes_with_blink(lv_obj_t *parent) {

	/* Fond écran noir */
	lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
	/* Œil gauche */
	face.left_eye = create_square_eye(parent, 30, 20, 80, 80);
	/* Œil droit */
	face.right_eye = create_square_eye(parent, 130, 20, 80, 80);

	/* --- Mains mignonnes (paume + doigts) --- */

	// Main gauche
	//	lv_obj_t* hand_left =
	create_hand(parent, 40, 200);
	// Main droite
	hand_right = create_hand(parent, 240 - 40 - 40, 200);

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

void draw_robot() {

	if (!robot_screen) {

		robot_screen = lv_obj_create(NULL);
		create_robot_square_eyes_with_blink(robot_screen);
		animate_robot_hand_right();
		create_full_click_zone(robot_screen);
	}
	lv_scr_load(robot_screen);

	// create_angry_eyes(lv_scr_act());
	//   lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
	//   lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
	//  draw_robot_mouth(lv_scr_act(),0,0,5);
}
