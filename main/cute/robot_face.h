#pragma once

#include "lvgl.h"

/* Deux yeux rouges en colère centrés dans le parent */
void create_angry_eyes(lv_obj_t *parent);

/* Œil rouge en colère à la position (x, y) ; retourne son conteneur */
lv_obj_t *create_angry_eye(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
						   lv_coord_t w, lv_coord_t h);

/* Sourire fixe sous les yeux */
void draw_robot_mouth(lv_obj_t *parent);

/* Œil carré arrondi bleu à la position (x, y) */
lv_obj_t *create_square_eye(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
							lv_coord_t w, lv_coord_t h);

/* Côté de l'œil sur l'écran (vu de face), pour orienter les expressions */
typedef enum {
	EYE_LEFT,
	EYE_RIGHT,
} eye_side_t;

/* Yeux expressifs façon robot EMO, basés sur create_square_eye.
 * Retournent le conteneur de l'œil (à supprimer avec lv_obj_del).
 * Les expressions sont dessinées avec des masques noirs : le fond derrière
 * l'œil doit être noir. */
lv_obj_t *create_square_eye_happy(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								  lv_coord_t w, lv_coord_t h);
lv_obj_t *create_square_eye_sad(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								lv_coord_t w, lv_coord_t h, eye_side_t side);
lv_obj_t *create_square_eye_angry(lv_obj_t *parent, lv_coord_t x,
								  lv_coord_t y, lv_coord_t w, lv_coord_t h,
								  eye_side_t side);
lv_obj_t *create_square_eye_tired(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
								  lv_coord_t w, lv_coord_t h);
