#include "lvgl.h"
#include "app_manager.h"
#define BABYBOT_COLOR 0x4DA6FF

static lv_obj_t * splash_scr;
static lv_obj_t * label_babybot;

static void babybot_anim_cb(void * var, int32_t v)
{
    lv_obj_t * obj = var;
    lv_obj_set_y(obj, v);
}

void babybot_splash_create()
{
    /* Crée un écran dédié */
    splash_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash_scr,
                              lv_color_black(),
                              0);
    lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);

    /* Label "baby bot" */
    label_babybot = lv_label_create(splash_scr);
    lv_label_set_text(label_babybot, "BABY BOT");
    lv_obj_set_style_text_color(label_babybot,
                                lv_color_hex(COLOR_MAIN),
                                0);
    lv_obj_set_style_text_font(label_babybot,
                               &lv_font_montserrat_32,
                               0);

    lv_obj_center(label_babybot);

    /* Animation de rebond vertical */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label_babybot);
    lv_anim_set_values(&a,
                       lv_obj_get_y(label_babybot) - 10,
                       lv_obj_get_y(label_babybot) + 10);
    lv_anim_set_time(&a, 600);
    lv_anim_set_playback_time(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, babybot_anim_cb);
    lv_anim_start(&a);

    /* Afficher l’écran */
    lv_scr_load(splash_scr);
}
