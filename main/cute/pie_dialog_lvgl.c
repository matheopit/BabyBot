/**
 * @file pie_dialog_lvgl.c
 * @brief Roue "camembert" (4 sections) dans une boite de dialogue modale
 *        LVGL, avec le meme style "fil de fer" que l'horloge : fond noir,
 *        traits/contours uniquement, couleur unique 0x4DA6FF.
 *
 * Ecran cible : 240 x 320.
 *
 * Style :
 *   - Fond du dialogue : noir pur.
 *   - Panneau : contour seul (bordure 0x4DA6FF), pas de remplissage colore.
 *   - Roue : cercle en contour + 4 rayons (spokes) qui delimitent les
 *     sections -- comme un camembert dessine au trait, pas rempli.
 *     Une section est "allumee" (remplissage semi-transparent de la meme
 *     couleur) uniquement pendant l'appui, pour le retour visuel.
 *   - Bouton de fermeture "X" : contour circulaire seul, pas de bouton
 *     plein.
 *
 * Fonctionnellement identique a la version precedente (hit-test par
 * quadrant sans trigonometrie, fermeture au clic exterieur / bouton X /
 * apres selection).
 */

#include "lvgl.h"
#include <stdio.h>
#include <stdint.h>
#include "app_manager.h"

#define SCREEN_W            240
#define SCREEN_H            320

#define PIE_RADIUS          90
#define PIE_DIAMETER        (PIE_RADIUS * 2)

#define WIRE_COLOR_HEX      0x4DA6FF

#define DIALOG_TITLE_H      30
#define DIALOG_PADDING      16
#define DIALOG_W            (PIE_DIAMETER + DIALOG_PADDING * 2)
#define DIALOG_H            (PIE_DIAMETER + DIALOG_TITLE_H + DIALOG_PADDING * 2)

/* Opacite du remplissage d'une section : quasi invisible au repos,
 * visible pendant l'appui (glow "wireframe") */
#define SECTION_FILL_OPA_IDLE     LV_OPA_TRANSP
#define SECTION_FILL_OPA_PRESSED  LV_OPA_40

/* ---------- Icones PNG a la place du texte ----------
 *
 * icon_section_1..4 : badges numerotes generes en 28x28px, deja en
 * couleur 0x4DA6FF, convertis en tableau C (format CF_TRUE_COLOR_ALPHA,
 * LV_COLOR_DEPTH=16) pour etre integres directement au binaire -- pas
 * de PNG ni de filesystem a l'execution.
 *
 * Fichiers a ajouter au projet : icon_section_1.c .. icon_section_4.c
 * et pie_icons.h (fournis a cote de ce fichier).
 *
 * Repasser PIE_USE_ICONS a 0 pour revenir au texte "Section 1".."4".
 */
#define PIE_USE_ICONS 1

#if PIE_USE_ICONS
#include "pie_icons.h"

static const lv_img_dsc_t *section_icons[4] = {
    &icon_section_1, &icon_section_2, &icon_section_3, &icon_section_4
};
#endif

/* ---------- Config des sections ---------- */

typedef struct {
    uint16_t angle_start;
    uint16_t angle_end;
    const char *label;
    int16_t label_dx;
    int16_t label_dy;
} pie_section_cfg_t;

static const pie_section_cfg_t sections_cfg[4] = {
    { 270,   0, "Reveil",  35, -35 },  /* haut-droit  */
    {   0,  90, "Horloge",  35,  35 },  /* bas-droit   */
    {  90, 180, "Music", -35,  35 },  /* bas-gauche  */
    { 180, 270, "BabyBot", -35, -35 },  /* haut-gauche */
};

static lv_color_t wire_color;

static lv_obj_t *pie_arcs[4];
static lv_obj_t *pie_hit_overlay;

/* ---------- Etat du dialogue ---------- */

static lv_obj_t *pie_dialog_modal_bg = NULL;
static lv_obj_t *pie_dialog_panel    = NULL;

static void pie_dialog_close(void);

/* ---------- Action metier appelee quand une section est choisie ---------- */

static void pie_section_action(uint32_t idx)
{
    printf("Section %u choisie\n", (unsigned)idx + 1);
    /* --> Ici : brancher l'action reelle associee a la section idx+1 */

   switch (idx) {
	case 0:
	  app_manager_choose_frame(FRAME_WAKEUP_SETTINGS);
	break;
	case 1:
	 app_manager_choose_frame(FRAME_HORLOGE);
	break;
	case 2:
	 app_manager_choose_frame(FRAME_MUSIC);
	break;
	case 3:
	 app_manager_choose_frame(FRAME_SMILE);
	break;
	default:;
   }



    /* Ferme automatiquement le dialogue apres selection.
     * Pour garder le dialogue ouvert, commenter la ligne suivante. */
    pie_dialog_close();
}

/* ---------- Hit-test par quadrant (sans trigonometrie) ---------- */

static int32_t pie_hit_test(int32_t dx, int32_t dy)
{
    int64_t dist_sq = (int64_t)dx * dx + (int64_t)dy * dy;
    if (dist_sq > (int64_t)PIE_RADIUS * PIE_RADIUS) return -1;

    if (dx >= 0 && dy <  0) return 0;  /* haut-droit  -> Section 1 */
    if (dx >= 0 && dy >= 0) return 1;  /* bas-droit   -> Section 2 */
    if (dx <  0 && dy >= 0) return 2;  /* bas-gauche  -> Section 3 */
    return 3;                          /* haut-gauche -> Section 4 */
}

static void pie_clear_pressed_visuals(void)
{
    for (uint32_t i = 0; i < 4; i++) {
        lv_obj_clear_state(pie_arcs[i], LV_STATE_PRESSED);
    }
}

static void pie_overlay_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_CLICKED &&
        code != LV_EVENT_PRESS_LOST) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t area;
    lv_obj_get_coords(pie_hit_overlay, &area);
    int32_t center_x = (area.x1 + area.x2) / 2;
    int32_t center_y = (area.y1 + area.y2) / 2;

    int32_t dx = p.x - center_x;
    int32_t dy = p.y - center_y;
    int32_t idx = pie_hit_test(dx, dy);

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        pie_clear_pressed_visuals();
        return;
    }

    pie_clear_pressed_visuals();
    if (idx >= 0) {
        lv_obj_add_state(pie_arcs[idx], LV_STATE_PRESSED);
    }

    if (code == LV_EVENT_CLICKED && idx >= 0) {
        pie_section_action((uint32_t)idx);
    }
}

/* ---------- Construction visuelle "fil de fer" de la roue ---------- */

/* Cercle exterieur en contour seul (identique dans l'esprit au cadran
 * de l'horloge) */
static void pie_wheel_build_outline(lv_obj_t *parent)
{
    lv_obj_t *outline = lv_obj_create(parent);
    lv_obj_remove_style_all(outline);
    lv_obj_set_size(outline, PIE_DIAMETER, PIE_DIAMETER);
    lv_obj_align(outline, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(outline, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outline, 2, 0);
    lv_obj_set_style_border_color(outline, wire_color, 0);
    lv_obj_clear_flag(outline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(outline, LV_OBJ_FLAG_SCROLLABLE);
}

/* 4 rayons (spokes) qui delimitent visuellement les 4 sections : aux
 * angles 0/90/180/270 (droite/bas/gauche/haut), donc des segments
 * parfaitement horizontaux/verticaux -> pas besoin de trigonometrie. */
static void pie_wheel_build_spokes(lv_obj_t *parent)
{
    /* Le conteneur fait PIE_DIAMETER x PIE_DIAMETER, centre est donc a
     * (PIE_RADIUS, PIE_RADIUS) en coordonnees locales. */
    static lv_point_t spoke_right[2];
    static lv_point_t spoke_down[2];
    static lv_point_t spoke_left[2];
    static lv_point_t spoke_up[2];

    spoke_right[0].x = PIE_RADIUS;              spoke_right[0].y = PIE_RADIUS;
    spoke_right[1].x = PIE_RADIUS * 2 - 2;       spoke_right[1].y = PIE_RADIUS;

    spoke_down[0].x  = PIE_RADIUS;               spoke_down[0].y = PIE_RADIUS;
    spoke_down[1].x  = PIE_RADIUS;                spoke_down[1].y = PIE_RADIUS * 2 - 2;

    spoke_left[0].x  = PIE_RADIUS;               spoke_left[0].y = PIE_RADIUS;
    spoke_left[1].x  = 2;                         spoke_left[1].y = PIE_RADIUS;

    spoke_up[0].x    = PIE_RADIUS;               spoke_up[0].y = PIE_RADIUS;
    spoke_up[1].x    = PIE_RADIUS;                spoke_up[1].y = 2;

    lv_point_t *spokes[4] = { spoke_right, spoke_down, spoke_left, spoke_up };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *line = lv_line_create(parent);
        lv_line_set_points(line, spokes[i], 2);
        lv_obj_set_style_line_color(line, wire_color, 0);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_rounded(line, false, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
}

/* Section "invisible" au repos : seul son remplissage s'allume (opacite)
 * pendant l'appui -- pas de couleur differente par section (monochrome,
 * coherent avec le style de l'horloge). */
static lv_obj_t *pie_section_visual_create(lv_obj_t *parent, uint32_t idx)
{
    const pie_section_cfg_t *cfg = &sections_cfg[idx];

    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, PIE_DIAMETER, PIE_DIAMETER);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);

    lv_arc_set_bg_angles(arc, cfg->angle_start, cfg->angle_end);
    lv_arc_set_angles(arc, cfg->angle_start, cfg->angle_end);

    lv_obj_set_style_arc_width(arc, PIE_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, PIE_RADIUS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_obj_set_style_arc_color(arc, wire_color, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, wire_color, LV_PART_INDICATOR);

    /* Repos : invisible. Appui : remplissage semi-transparent "glow". */
    lv_obj_set_style_arc_opa(arc, SECTION_FILL_OPA_IDLE, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, SECTION_FILL_OPA_IDLE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, SECTION_FILL_OPA_PRESSED,
                              LV_PART_INDICATOR | LV_STATE_PRESSED);

    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);

    return arc;
}

static void pie_label_place(lv_obj_t *parent, uint32_t idx)
{
    const pie_section_cfg_t *cfg = &sections_cfg[idx];

#if PIE_USE_ICONS
    /* Icone deja coloree en 0x4DA6FF (generee ainsi) : pas besoin de
     * recolor ici. Si vous repassez a des icones blanches/alpha-only,
     * reactivez lv_obj_set_style_img_recolor(...) pour les teinter. */
    lv_obj_t *icon = lv_img_create(parent);
    lv_img_set_src(icon, section_icons[idx]);
    lv_obj_align(icon, LV_ALIGN_CENTER, cfg->label_dx, cfg->label_dy);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(icon);
#else
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, cfg->label);
    lv_obj_set_style_text_color(lbl, wire_color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, cfg->label_dx, cfg->label_dy);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(lbl);
#endif
}

/* Construit la roue dans le conteneur donne (deja dimensionne en
 * PIE_DIAMETER x PIE_DIAMETER par l'appelant) */
static void pie_wheel_build(lv_obj_t *wheel_container)
{
    pie_wheel_build_outline(wheel_container);
    pie_wheel_build_spokes(wheel_container);

    for (uint32_t i = 0; i < 4; i++) {
        pie_arcs[i] = pie_section_visual_create(wheel_container, i);
    }

    /* Point de pivot central : seul element reellement "rempli" */
    lv_obj_t *center_dot = lv_obj_create(wheel_container);
    lv_obj_remove_style_all(center_dot);
    lv_obj_set_size(center_dot, 6, 6);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, wire_color, 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(center_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(center_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(center_dot);

    for (uint32_t i = 0; i < 4; i++) {
        pie_label_place(wheel_container, i);
    }

    pie_hit_overlay = lv_obj_create(wheel_container);
    lv_obj_remove_style_all(pie_hit_overlay);
    lv_obj_set_size(pie_hit_overlay, PIE_DIAMETER, PIE_DIAMETER);
    lv_obj_align(pie_hit_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(pie_hit_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pie_hit_overlay, 0, 0);
    lv_obj_add_flag(pie_hit_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pie_hit_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(pie_hit_overlay);

    lv_obj_add_event_cb(pie_hit_overlay, pie_overlay_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pie_hit_overlay, pie_overlay_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(pie_hit_overlay, pie_overlay_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(pie_hit_overlay, pie_overlay_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(pie_hit_overlay, pie_overlay_event_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------- Boite de dialogue modale, style fil de fer ---------- */

static void pie_dialog_modal_bg_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(e) != pie_dialog_modal_bg) return;
    pie_dialog_close();
}

static void pie_dialog_close_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    pie_dialog_close();
}

static void pie_dialog_close(void)
{
    if (pie_dialog_modal_bg == NULL) return;
    lv_obj_del(pie_dialog_modal_bg);
    pie_dialog_modal_bg = NULL;
    pie_dialog_panel = NULL;
}

/**
 * @brief Ouvre la boite de dialogue contenant la roue camembert, style
 *        fil de fer (fond noir, contours 0x4DA6FF).
 * @param parent_scr Ecran parent (ex: lv_scr_act())
 */
void pie_dialog_open(lv_obj_t *parent_scr)
{
    if (pie_dialog_modal_bg != NULL) return;

    wire_color = lv_color_hex(WIRE_COLOR_HEX);

    /* 1) Fond modal plein ecran : noir, semi-transparent, cliquable pour fermer */
    pie_dialog_modal_bg = lv_obj_create(parent_scr);
    lv_obj_remove_style_all(pie_dialog_modal_bg);
    lv_obj_set_size(pie_dialog_modal_bg, SCREEN_W, SCREEN_H);
    lv_obj_align(pie_dialog_modal_bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(pie_dialog_modal_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pie_dialog_modal_bg, LV_OPA_70, 0);
    lv_obj_add_flag(pie_dialog_modal_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pie_dialog_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pie_dialog_modal_bg, pie_dialog_modal_bg_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(pie_dialog_modal_bg);

    /* 2) Panneau : fond noir pur, contour seul (pas de remplissage colore) */
    pie_dialog_panel = lv_obj_create(pie_dialog_modal_bg);
    lv_obj_remove_style_all(pie_dialog_panel);
    lv_obj_set_size(pie_dialog_panel, DIALOG_W, DIALOG_H);
    lv_obj_align(pie_dialog_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(pie_dialog_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pie_dialog_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pie_dialog_panel, 8, 0);
    lv_obj_set_style_border_width(pie_dialog_panel, 1, 0);
    lv_obj_set_style_border_color(pie_dialog_panel, wire_color, 0);
    lv_obj_set_style_pad_all(pie_dialog_panel, DIALOG_PADDING, 0);
    lv_obj_add_flag(pie_dialog_panel, LV_OBJ_FLAG_CLICKABLE); /* absorbe le clic, ne ferme pas */
    lv_obj_clear_flag(pie_dialog_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 3) Titre */
    lv_obj_t *title = lv_label_create(pie_dialog_panel);
    lv_label_set_text(title, "Choisir une section");
    lv_obj_set_style_text_color(title, wire_color, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    /* 4) Bouton de fermeture "X" : contour seul, pas de bouton plein */
    lv_obj_t *close_btn = lv_btn_create(pie_dialog_panel);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 24, 24);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_border_color(close_btn, wire_color, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, pie_dialog_close_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, wire_color, 0);
    lv_obj_center(close_lbl);
    lv_obj_clear_flag(close_lbl, LV_OBJ_FLAG_CLICKABLE);

    /* 5) Conteneur de la roue, sous le titre */
    lv_obj_t *wheel_container = lv_obj_create(pie_dialog_panel);
    lv_obj_remove_style_all(wheel_container);
    lv_obj_set_size(wheel_container, PIE_DIAMETER, PIE_DIAMETER);
    lv_obj_align(wheel_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(wheel_container, LV_OBJ_FLAG_SCROLLABLE);

    pie_wheel_build(wheel_container);
}


// Déclaration du handler
static void full_click_event(lv_event_t *e) {
	if (e->code == LV_EVENT_CLICKED) {
		//app_manager_choose_frame(FRAME_WAKEUP_SETTINGS);
	   pie_dialog_open(lv_scr_act());
	}
}

void create_full_click_zone(lv_obj_t *parent) {
	lv_obj_t *zone = lv_obj_create(parent);

	lv_obj_set_size(zone, LV_HOR_RES, LV_VER_RES); // 240x320 automatiquement
	lv_obj_align(zone, LV_ALIGN_CENTER, 0, 0);

	// Invisible
	lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(zone, 0, 0);

	// Pas de scroll
	lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);

	// Zone cliquable
	lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);

	// Callback à l’ancienne
	lv_obj_add_event_cb(zone, full_click_event, LV_EVENT_CLICKED, NULL);
}


