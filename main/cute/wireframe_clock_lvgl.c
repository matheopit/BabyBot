/**
 * @file wireframe_clock_lvgl.c
 * @brief Horloge analogique animee, style "fil de fer" (wireframe), en LVGL.
 *
 * - Fond noir.
 * - Tout est dessine en traits (LV_PART lines / bordures), aucune surface
 *   remplie sauf le petit point de pivot central : cadran = cercle en
 *   contour seul, graduations = lignes, aiguilles = lignes.
 * - Couleur unique : 0x4DA6FF.
 * - Animation : les aiguilles sont recalculees chaque seconde via un
 *   lv_timer (mouvement "pas a pas" realiste d'horloge, pas de sweep
 *   continu -- voir la note en bas de fichier pour un balayage fluide).
 * - Deux "yeux" LED ronds au-dessus du cadran (meme couleur), qui
 *   clignent periodiquement pour un cote "petit robot mignon".
 * - Affichage numerique HH:MM:SS (24h) sous le cadran, mis a jour chaque
 *   seconde en meme temps que les aiguilles.
 * - Un clic n'importe ou sur l'ecran declenche un callback utilisateur
 *   (a enregistrer via wireframe_clock_set_click_cb), typiquement pour
 *   changer d'ecran/frame.
 *
 * Ecran cible : 240 x 320, horloge centree. Repartition verticale :
 *   yeux (haut) / cadran analogique (milieu) / heure numerique (bas).
 *
 * DEPENDANCE : ce fichier utilise sinf()/cosf() de <math.h> pour placer
 * les graduations et les aiguilles -> penser a lier la libm (-lm) sur
 * la toolchain cible.
 *
 * SOURCE DE L'HEURE : get_current_time() utilise <time.h> (time()/
 * localtime()), pratique pour tester sur simulateur PC (lv_drivers/SDL).
 * Sur cible embarquee reelle, remplacer son contenu par une lecture RTC
 * (ex: HAL_RTC_GetTime() sur STM32).
 */

#include "lvgl.h"
#include <math.h>
#include <time.h>
#include <stdint.h>
#include "draw_function.h"

#define SCREEN_W        240
#define SCREEN_H        320
#define CLOCK_CENTER_X  (SCREEN_W / 2)   /* 120 */
#define CLOCK_CENTER_Y  (SCREEN_H / 2)   /* 160 */
#define CLOCK_RADIUS    90
#define CLOCK_DIAMETER  (CLOCK_RADIUS * 2)

#define CLOCK_COLOR_HEX 0x4DA6FF

/* Longueurs des aiguilles, en fraction du rayon */
#define HAND_LEN_HOUR   (CLOCK_RADIUS * 0.50f)
#define HAND_LEN_MIN    (CLOCK_RADIUS * 0.75f)
#define HAND_LEN_SEC    (CLOCK_RADIUS * 0.85f)

/* Yeux LED : places dans la marge au-dessus du cadran (0 .. CENTER_Y-RADIUS) */
#define EYE_DIAMETER        16
#define EYE_MIN_HEIGHT      2      /* hauteur "yeux fermes" pendant le clignement */
#define EYE_SPACING_X       26     /* ecart au centre pour chaque oeil */
#define EYE_Y_OFFSET        (-(CLOCK_RADIUS) - 28) /* offset / centre de l'ecran, vers le haut */
#define EYE_BLINK_PERIOD_MS 4000
#define EYE_BLINK_ANIM_MS   110

/* Affichage numerique : place dans la marge sous le cadran */
#define DIGITAL_Y_OFFSET    (CLOCK_RADIUS + 28) /* offset / centre de l'ecran, vers le bas */

static lv_color_t clock_color;

static lv_obj_t *hand_hour;
static lv_obj_t *hand_min;
static lv_obj_t *hand_sec;

static lv_obj_t *eye_left;
static lv_obj_t *eye_right;
static lv_timer_t *eye_blink_timer = NULL;

static lv_obj_t *digital_label;

typedef void (*wireframe_clock_click_cb_t)(void);
static wireframe_clock_click_cb_t clock_click_cb = NULL;

/* Tableaux de points persistants : lv_line garde juste un pointeur,
 * il faut donc que ces buffers restent vivants (pas de variables locales). */
static lv_point_t hour_pts[2];
static lv_point_t min_pts[2];
static lv_point_t sec_pts[2];
static lv_point_t tick_pts[12][2];
static lv_obj_t *horloge_screen = NULL;
static lv_timer_t *clock_update_timer = NULL;

/* ---------- Source de l'heure (a adapter sur cible embarquee) ---------- */

/* h24 : 0-23 (pour l'affichage numerique) ; h12 : 0-11 (pour l'aiguille) */
static void get_current_time(uint8_t *h24, uint8_t *h12, uint8_t *m, uint8_t *s)
{
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    *h24 = (uint8_t)tmv->tm_hour;
    *h12 = (uint8_t)(tmv->tm_hour % 12);
    *m   = (uint8_t)tmv->tm_min;
    *s   = (uint8_t)tmv->tm_sec;

    /* --- Remplacement typique sur cible embarquee (exemple STM32 HAL) ---
     * RTC_TimeTypeDef t;
     * HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
     * *h24 = t.Hours; *h12 = t.Hours % 12; *m = t.Minutes; *s = t.Seconds;
     */
}

/* ---------- Geometrie ---------- */

/* Angle LVGL/ecran : 0 deg = 3h (droite), sens horaire.
 * On decale de -90 deg pour que 0 corresponde a 12h (en haut). */
static void polar_point(float angle_deg, float len, lv_point_t *out)
{
    float rad = (angle_deg - 90.0f) * (float)M_PI / 180.0f;
    out->x = (lv_coord_t)(CLOCK_CENTER_X + cosf(rad) * len);
    out->y = (lv_coord_t)(CLOCK_CENTER_Y + sinf(rad) * len);
}

/* ---------- Construction statique : cadran + graduations ---------- */

static void wireframe_clock_build_face(lv_obj_t *parent)
{
    /* Cercle du cadran : contour seul, pas de remplissage (wireframe) */
    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, CLOCK_DIAMETER, CLOCK_DIAMETER);
    lv_obj_align(face, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face, 2, 0);
    lv_obj_set_style_border_color(face, clock_color, 0);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);

    /* 12 graduations : plus longues/epaisses sur 12h/3h/6h/9h */
    for (int i = 0; i < 12; i++) {
        float angle = i * 30.0f;
        int is_major = (i % 3 == 0);
        float outer_r = CLOCK_RADIUS - 2;
        float inner_r = is_major ? (CLOCK_RADIUS - 16) : (CLOCK_RADIUS - 9);

        polar_point(angle, outer_r, &tick_pts[i][0]);
        polar_point(angle, inner_r, &tick_pts[i][1]);

        lv_obj_t *tick = lv_line_create(parent);
        lv_line_set_points(tick, tick_pts[i], 2);
        lv_obj_set_style_line_color(tick, clock_color, 0);
        lv_obj_set_style_line_width(tick, is_major ? 3 : 1, 0);
        lv_obj_set_style_line_rounded(tick, false, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
    }
}

static lv_obj_t *wireframe_clock_hand_create(lv_obj_t *parent, uint8_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_color(line, clock_color, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
}

/* ---------- Mise a jour animee (appelee chaque seconde) ---------- */

static void clock_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    uint8_t h24, h12, m, s;
    get_current_time(&h24, &h12, &m, &s);

    float sec_angle  = s * 6.0f;                              /* 360/60 */
    float min_angle  = m * 6.0f + s * 0.1f;                    /* + glissement doux */
    float hour_angle = h12 * 30.0f + m * 0.5f;                 /* 360/12 + glissement */

    hour_pts[0].x = CLOCK_CENTER_X; hour_pts[0].y = CLOCK_CENTER_Y;
    polar_point(hour_angle, HAND_LEN_HOUR, &hour_pts[1]);

    min_pts[0].x = CLOCK_CENTER_X; min_pts[0].y = CLOCK_CENTER_Y;
    polar_point(min_angle, HAND_LEN_MIN, &min_pts[1]);

    sec_pts[0].x = CLOCK_CENTER_X; sec_pts[0].y = CLOCK_CENTER_Y;
    polar_point(sec_angle, HAND_LEN_SEC, &sec_pts[1]);

    lv_line_set_points(hand_hour, hour_pts, 2);
    lv_line_set_points(hand_min,  min_pts,  2);
    lv_line_set_points(hand_sec,  sec_pts,  2);

    /* Affichage numerique 24h, mis a jour en meme temps que les aiguilles */
    lv_label_set_text_fmt(digital_label, "%02u:%02u:%02u", h24, m, s);
}

/* ---------- Yeux LED "robot mignon" ---------- */

static lv_obj_t *eye_create(lv_obj_t *parent, int16_t dx)
{
    lv_obj_t *eye = lv_obj_create(parent);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, EYE_DIAMETER, EYE_DIAMETER);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye, clock_color, 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    /* Petite lueur type LED : ombre floue de la meme couleur */
    lv_obj_set_style_shadow_color(eye, clock_color, 0);
    lv_obj_set_style_shadow_width(eye, 10, 0);
    lv_obj_set_style_shadow_spread(eye, 1, 0);
    lv_obj_set_style_shadow_opa(eye, LV_OPA_70, 0);
    lv_obj_align(eye, LV_ALIGN_CENTER, dx, EYE_Y_OFFSET);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    return eye;
}

/* Pendant le clignement, la hauteur de l'oeil est reduite puis restauree ;
 * on realigne a chaque pas pour que l'oeil reste centre sur sa position
 * (align recalcule le coin haut-gauche a partir du centre + offset fixe). */
static void eye_blink_anim_exec_cb(void *var, int32_t v)
{
    lv_obj_t *eye = (lv_obj_t *)var;
    lv_obj_set_height(eye, v);
    int16_t dx = (eye == eye_left) ? -EYE_SPACING_X : EYE_SPACING_X;
    lv_obj_align(eye, LV_ALIGN_CENTER, dx, EYE_Y_OFFSET);
}

static void eye_blink_once(lv_obj_t *eye)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, eye);
    lv_anim_set_exec_cb(&a, eye_blink_anim_exec_cb);
    lv_anim_set_values(&a, EYE_DIAMETER, EYE_MIN_HEIGHT);
    lv_anim_set_time(&a, EYE_BLINK_ANIM_MS);
    lv_anim_set_playback_time(&a, EYE_BLINK_ANIM_MS); /* revient automatiquement */
    lv_anim_start(&a);
}

static void eye_blink_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    eye_blink_once(eye_left);
    eye_blink_once(eye_right);
}

/**
 * @brief Construit l'horloge animee wireframe sur l'ecran/conteneur donne.
 * @param parent Ecran ou conteneur parent (ex: lv_scr_act())
 */
void wireframe_clock_create(lv_obj_t *parent)
{
    clock_color = lv_color_hex(CLOCK_COLOR_HEX);

    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Clic n'importe ou sur l'ecran -> callback utilisateur. Les elements
     * du dessin (lignes, yeux, label) sont tous non-cliquables, donc le
     * clic remonte toujours jusqu'ici, quel que soit l'endroit touche. */
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    wireframe_clock_build_face(parent);

    /* Yeux LED "robot mignon", au-dessus du cadran */
    eye_left  = eye_create(parent, -EYE_SPACING_X);
    eye_right = eye_create(parent,  EYE_SPACING_X);
    eye_blink_timer = lv_timer_create(eye_blink_timer_cb, EYE_BLINK_PERIOD_MS, NULL);

    /* Aiguilles : epaisseurs differentes pour la lisibilite, meme couleur
     * partout (esthetique fil de fer sobre) */
    hand_hour = wireframe_clock_hand_create(parent, 4);
    hand_min  = wireframe_clock_hand_create(parent, 3);
    hand_sec  = wireframe_clock_hand_create(parent, 1);

    /* Point de pivot central : seul element "rempli" du dessin */
    lv_obj_t *pivot = lv_obj_create(parent);
    lv_obj_remove_style_all(pivot);
    lv_obj_set_size(pivot, 8, 8);
    lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pivot, clock_color, 0);
    lv_obj_set_style_bg_opa(pivot, LV_OPA_COVER, 0);
    lv_obj_align(pivot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(pivot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(pivot);

    /* Affichage numerique HH:MM:SS (24h), sous le cadran */
    digital_label = lv_label_create(parent);
    lv_obj_set_style_text_color(digital_label, clock_color, 0);
    lv_obj_set_style_text_font(digital_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(digital_label, 2, 0);
    lv_obj_align(digital_label, LV_ALIGN_CENTER, 0, DIGITAL_Y_OFFSET);
    lv_obj_clear_flag(digital_label, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(digital_label, "00:00:00"); /* place-holder avant 1ere maj */

    /* Premiere mise a jour immediate, puis toutes les secondes */
    clock_timer_cb(NULL);
    clock_update_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
}

/**
 * @brief A appeler si l'ecran contenant l'horloge est detruit, pour
 *        stopper le timer et eviter un acces a des objets supprimes.
 */
void wireframe_clock_destroy(void)
{
    if (clock_update_timer != NULL) {
        lv_timer_del(clock_update_timer);
        clock_update_timer = NULL;
    }
    if (eye_blink_timer != NULL) {
        lv_timer_del(eye_blink_timer);
        eye_blink_timer = NULL;
    }
}

void  clock_create(){
	if (!horloge_screen) {
      horloge_screen = lv_obj_create(NULL);
      wireframe_clock_create(horloge_screen);
      create_full_click_zone(horloge_screen);
    }
    lv_scr_load(horloge_screen);
}



/* Exemple d'utilisation :

 *
 *   static void on_clock_clicked(void)
 *   {
 *       // --> changer d'ecran/frame ici, ex: lv_scr_load(next_screen);
 *   }
 *
 *   lv_obj_t *scr = lv_scr_act();
 *   wireframe_clock_create(scr);
 *   wireframe_clock_set_click_cb(on_clock_clicked);
 *
 * NOTE - balayage fluide de la trotteuse :
 * Pour un mouvement continu (au lieu du pas par pas chaque seconde),
 * remplacer la periode du timer par ~30-50 ms et calculer sec_angle avec
 * la fraction de seconde ecoulee (via lv_tick_get() ou une horloge RTC
 * sub-seconde), ex: sec_angle = (s + ms/1000.0f) * 6.0f;
 *
 * NOTE - repartition verticale (a ajuster si CLOCK_RADIUS change) :
 * yeux a EYE_Y_OFFSET, cadran de -CLOCK_RADIUS a +CLOCK_RADIUS, heure
 * numerique a DIGITAL_Y_OFFSET, tous relatifs au centre de l'ecran.
 */
