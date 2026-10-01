#include "app_manager.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#define BABYBOT_COLOR 0x4DA6FF

static lv_obj_t *splash_scr;
static lv_obj_t *label_babybot;

// Le zoom (transform_zoom) d'un label demande LV_COLOR_SCREEN_TRANSP, qui
// n'est pas activé : on zoome donc une image (snapshot) du label.
static lv_img_dsc_t babybot_img_dsc;
static void *babybot_img_buf = NULL;

static void babybot_anim_cb(void *var, int32_t v) {
	lv_img_set_zoom(var, v);
}

static void splash_delete_cb(lv_event_t *e) {
	LV_UNUSED(e);
	heap_caps_free(babybot_img_buf);
	babybot_img_buf = NULL;
	splash_scr = NULL;
	label_babybot = NULL;
}

// Le splash ne sert qu'au démarrage : on le supprime dès qu'un autre écran
// le remplace (après l'événement, d'où le _async)
static void splash_unloaded_cb(lv_event_t *e) {
	lv_obj_del_async(lv_event_get_target(e));
}

void babybot_splash_create() {
	/* Crée un écran dédié */
	splash_scr = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(splash_scr, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);
	lv_obj_add_event_cb(splash_scr, splash_delete_cb, LV_EVENT_DELETE, NULL);
	lv_obj_add_event_cb(splash_scr, splash_unloaded_cb, LV_EVENT_SCREEN_UNLOADED,
						NULL);

	/* Label "baby bot" */
	label_babybot = lv_label_create(splash_scr);
	lv_label_set_text(label_babybot, "BABY BOT");
	lv_obj_set_style_text_color(label_babybot, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_set_style_text_font(label_babybot, &lv_font_montserrat_32, 0);

	lv_obj_center(label_babybot);

	/* Image du label, sur fond noir comme l'écran (pas besoin d'alpha) */
	lv_obj_update_layout(label_babybot);
	uint32_t size =
		lv_snapshot_buf_size_needed(label_babybot, LV_IMG_CF_TRUE_COLOR);
	heap_caps_free(babybot_img_buf);
	babybot_img_buf = heap_caps_malloc(size, MALLOC_CAP_8BIT);
	if (babybot_img_buf == NULL ||
		lv_snapshot_take_to_buf(label_babybot, LV_IMG_CF_TRUE_COLOR,
								&babybot_img_dsc, babybot_img_buf,
								size) != LV_RES_OK) {
		/* Pas d'image : label fixe, sans animation */
		lv_scr_load(splash_scr);
		return;
	}

	lv_obj_t *img = lv_img_create(splash_scr);
	lv_img_set_src(img, &babybot_img_dsc);
	lv_obj_center(img);
	lv_obj_del(label_babybot);
	label_babybot = img;

	/* Animation de zoom autour du centre (LV_IMG_ZOOM_NONE = taille normale) */
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, label_babybot);
	lv_anim_set_values(&a, LV_IMG_ZOOM_NONE * 3 / 4, LV_IMG_ZOOM_NONE * 5 / 4);
	lv_anim_set_time(&a, 600);
	lv_anim_set_playback_time(&a, 600);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_set_exec_cb(&a, babybot_anim_cb);
	lv_anim_start(&a);

	/* Afficher l’écran */
	lv_scr_load(splash_scr);
}
