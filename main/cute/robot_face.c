#include "robot_face.h"

#include "app_manager.h"

#include <math.h>

/* Zone des yeux en colère (centrée dans le parent) */
#define ANGRY_EYES_W 240
#define ANGRY_EYES_H 120

/* Crée un objet rectangle arrondi plein, sans bordure ni padding */
static lv_obj_t *create_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
							  lv_coord_t w, lv_coord_t h, lv_color_t color) {
	lv_obj_t *obj = lv_obj_create(parent);
	lv_obj_set_size(obj, w, h);
	lv_obj_set_style_bg_color(obj, color, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(obj, x, y);
	return obj;
}

/* Disque plein de centre (cx, cy) et de rayon r */
static lv_obj_t *create_disc(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy,
							 lv_coord_t r, lv_color_t color) {
	return create_shape(parent, cx - r, cy - r, 2 * r, 2 * r, color);
}

/* lv_line garde un pointeur vers ses points : on les libère avec l'objet */
static void free_line_points_cb(lv_event_t *e) {
	lv_mem_free(lv_event_get_user_data(e));
}

/* Ligne de (x1, y1) à (x2, y2) ; les points de lv_line doivent être
 * positifs, la ligne est donc placée au coin haut-gauche du segment */
static lv_obj_t *create_line(lv_obj_t *parent, lv_coord_t x1, lv_coord_t y1,
							 lv_coord_t x2, lv_coord_t y2, lv_coord_t width,
							 lv_color_t color) {
	lv_coord_t x0 = LV_MIN(x1, x2);
	lv_coord_t y0 = LV_MIN(y1, y2);

	lv_point_t *pts = lv_mem_alloc(2 * sizeof(lv_point_t));
	LV_ASSERT_MALLOC(pts);
	pts[0] = (lv_point_t){x1 - x0, y1 - y0};
	pts[1] = (lv_point_t){x2 - x0, y2 - y0};

	lv_obj_t *line = lv_line_create(parent);
	lv_line_set_points(line, pts, 2);
	lv_obj_set_style_line_color(line, color, 0);
	lv_obj_set_style_line_width(line, width, 0);
	lv_obj_set_pos(line, x0, y0);
	lv_obj_add_event_cb(line, free_line_points_cb, LV_EVENT_DELETE, pts);
	return line;
}

/* Œil rouge en colère dans un rectangle (x, y, w, h), comme
 * create_square_eye. Retourne le conteneur de l'œil. */
lv_obj_t *create_angry_eye(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
						   lv_coord_t w, lv_coord_t h) {
	/* Conteneur transparent ; le sourcil peut dépasser des bords */
	lv_obj_t *eye = lv_obj_create(parent);
	lv_obj_set_size(eye, w, h);
	lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(eye, 0, 0);
	lv_obj_set_style_pad_all(eye, 0, 0);
	lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(eye, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
	lv_obj_set_pos(eye, x, y);

	/* Coordonnées relatives au conteneur */
	lv_coord_t cx = w / 2;
	lv_coord_t cy = h / 2;
	lv_coord_t r = LV_MIN(w, h) / 2;

	/* Iris rouge (créé en premier : dessiné en dessous) */
	create_shape(eye, 0, 0, w, h, lv_color_hex(0xFF3B30));

	/* Pupille verticale (colère) */
	create_shape(eye, cx - r / 6, cy - r / 2, r / 3, r, lv_color_black());

	/* Reflet blanc */
	create_disc(eye, cx - r / 3, cy - r / 3, r / 6, lv_color_white());

	/* Sourcil incliné (colère) */
	create_line(eye, cx - r, cy - r,	 // début
				cx + r / 2, cy - r / 2, // fin
				6, lv_color_black());

	return eye;
}

/* Deux yeux en colère centrés dans le parent */
void create_angry_eyes(lv_obj_t *parent) {
	lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

	/* Zone de 240x120 centrée, comme l'ancien canvas */
	lv_obj_update_layout(parent);
	lv_coord_t x0 = (lv_obj_get_content_width(parent) - ANGRY_EYES_W) / 2;
	lv_coord_t y0 = (lv_obj_get_content_height(parent) - ANGRY_EYES_H) / 2;

	/* Œil gauche */
	create_angry_eye(parent, x0 + 40, y0 + 30, 60, 60);

	/* Œil droit */
	create_angry_eye(parent, x0 + 140, y0 + 30, 60, 60);
}

/* Sourire fixe sous les yeux : un arc LVGL (pas de canvas ni de buffer) */
void draw_robot_mouth(lv_obj_t *parent) {
	lv_obj_t *mouth = lv_arc_create(parent);

	/* Cercle de 80 px centré en x ; seul le bas de l'arc est dessiné */
	lv_obj_set_size(mouth, 80, 80);
	lv_obj_set_pos(mouth, 120 - 40, 60);

	/* Angles LVGL : 0° à droite, 90° en bas → 30°..150° = sourire */
	lv_arc_set_rotation(mouth, 0);
	lv_arc_set_bg_angles(mouth, 30, 150);

	/* Trait du sourire */
	lv_obj_set_style_arc_color(mouth, lv_color_hex(COLOR_MAIN), LV_PART_MAIN);
	lv_obj_set_style_arc_width(mouth, 6, LV_PART_MAIN);
	lv_obj_set_style_arc_rounded(mouth, true, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(mouth, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_pad_all(mouth, 0, LV_PART_MAIN);

	/* Pas d'indicateur ni de bouton : ce n'est pas un slider */
	lv_obj_set_style_arc_opa(mouth, LV_OPA_TRANSP, LV_PART_INDICATOR);
	lv_obj_remove_style(mouth, NULL, LV_PART_KNOB);
	lv_obj_clear_flag(mouth, LV_OBJ_FLAG_CLICKABLE);
}

/* Crée un œil carré arrondi bleu */
lv_obj_t *create_square_eye(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
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

/* --- Yeux expressifs façon robot EMO ---
 * Un œil carré bleu dans un conteneur qui coupe ce qui dépasse ; l'expression
 * est obtenue en masquant une partie de l'œil avec des formes noires (même
 * couleur que le fond de l'écran). */

/* Conteneur transparent + œil carré ; les enfants sont coupés aux bords */
static lv_obj_t *create_emo_eye_base(lv_obj_t *parent, lv_coord_t x,
									 lv_coord_t y, lv_coord_t w, lv_coord_t h) {
	lv_obj_t *eye = lv_obj_create(parent);
	lv_obj_set_size(eye, w, h);
	lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(eye, 0, 0);
	lv_obj_set_style_radius(eye, 0, 0);
	lv_obj_set_style_pad_all(eye, 0, 0);
	lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(eye, x, y);

	create_square_eye(eye, 0, 0, w, h);
	return eye;
}

/* Masque en noir tout ce qui est au-dessus de la droite allant de
 * (0, y_left) à (w, y_right) : une ligne très épaisse décalée vers le haut */
static void mask_above(lv_obj_t *eye, lv_coord_t w, lv_coord_t h,
					   lv_coord_t y_left, lv_coord_t y_right) {
	lv_coord_t t = h; // épaisseur du masque
	lv_coord_t ext = t / 2;
	float slope = (float)(y_right - y_left) / w;
	/* Décalage vertical pour que le bord bas de la ligne tombe sur la droite */
	lv_coord_t off = (lv_coord_t)(t / 2.0f * sqrtf(1.0f + slope * slope));

	create_line(eye, -ext, y_left - (lv_coord_t)(slope * ext) - off, //
				w + ext, y_right + (lv_coord_t)(slope * ext) - off,	 //
				t, lv_color_black());
}

/* Œil heureux : seul le haut reste visible, en forme de ^ */
lv_obj_t *create_square_eye_happy(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								  lv_coord_t w, lv_coord_t h) {
	lv_obj_t *eye = create_emo_eye_base(parent, x, y, w, h);

	/* Grande ellipse noire qui mange le bas de l'œil */
	create_shape(eye, -w / 4, h * 45 / 100, w * 3 / 2, h, lv_color_black());
	return eye;
}

/* Œil triste : coin extérieur du haut coupé, la paupière tombe vers
 * l'extérieur */
lv_obj_t *create_square_eye_sad(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								lv_coord_t w, lv_coord_t h, eye_side_t side) {
	lv_obj_t *eye = create_emo_eye_base(parent, x, y, w, h);

	lv_coord_t low = h * 45 / 100;
	if (side == EYE_LEFT)
		mask_above(eye, w, h, low, 0); // extérieur = gauche
	else
		mask_above(eye, w, h, 0, low); // extérieur = droite
	return eye;
}

/* Œil en colère : coin intérieur du haut coupé, sourcils froncés vers le nez */
lv_obj_t *create_square_eye_angry(lv_obj_t *parent, lv_coord_t x,
								  lv_coord_t y, lv_coord_t w, lv_coord_t h,
								  eye_side_t side) {
	lv_obj_t *eye = create_emo_eye_base(parent, x, y, w, h);

	lv_coord_t low = h * 45 / 100;
	if (side == EYE_LEFT)
		mask_above(eye, w, h, 0, low); // intérieur = droite
	else
		mask_above(eye, w, h, low, 0); // intérieur = gauche
	return eye;
}

/* Œil fatigué : paupière lourde, la moitié haute de l'œil est cachée */
lv_obj_t *create_square_eye_tired(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								  lv_coord_t w, lv_coord_t h) {
	lv_obj_t *eye = create_emo_eye_base(parent, x, y, w, h);

	lv_coord_t lid = h * 55 / 100;
	mask_above(eye, w, h, lid, lid);
	return eye;
}
