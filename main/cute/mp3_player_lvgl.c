/**
 * @file mp3_player_lvgl.c
 * @brief Lecteur MP3 LVGL, meme theme "fil de fer" que les autres frames
 *        (fond noir, contours/texte en 0x4DA6FF, pas de remplissage
 *        colore sauf la barre de progression).
 *
 * Contenu de l'ecran :
 *   - Titre du morceau en cours (defile si le nom est trop long).
 *   - Barre de progression + temps ecoule / duree totale (mm:ss).
 *   - Controles : precedent / lecture-pause (bouton unique qui bascule
 *     d'icone) / suivant.
 *   - Reglage du son : icone haut-parleur (clic = mute/unmute) + slider.
 *   - Bouton "Parcourir" qui ouvre une liste modale des fichiers .mp3
 *     trouves sur la carte SD (via <dirent.h> ou l'API lv_fs).
 *   - Zone cliquable en bas de l'ecran : comportement LIBRE, non
 *     implemente ici -- juste un callback a enregistrer (voir
 *     mp3_player_set_bottom_click_cb), meme principe que
 *     wireframe_clock_set_click_cb dans l'horloge.
 *
 * Ecran cible : 240 x 320.
 *
 * ============================================================
 * IMPORTANT - ce fichier ne fait QUE l'IHM. Il ne decode ni ne joue
 * aucun son. L'integration reelle (codec I2S/DAC, decodeur MP3, etc.)
 * doit fournir les fonctions listees dans la section "A IMPLEMENTER"
 * plus bas.
 * ============================================================
 *
 * CARTE SD : deux modes possibles, choisis via MP3_USE_POSIX_FS plus bas.
 *   - MP3_USE_POSIX_FS=1 (par defaut) : acces direct via <dirent.h>
 *     (opendir/readdir), chemin POSIX classique, ex "/sdcard". Adapte a
 *     une cible avec un vrai systeme de fichiers Linux (ex: iMX6ULL avec
 *     la carte SD montee sur /sdcard).
 *   - MP3_USE_POSIX_FS=0 : passe par l'API filesystem virtuelle de LVGL
 *     (lv_fs_dir_open/read), qui exige un prefixe "lettre de lecteur"
 *     enregistre via lv_fs_drv_register (ex: "S:/sdcard", pas juste
 *     "/sdcard"). Necessaire sur cible bare-metal (STM32 + FatFs) ou le
 *     systeme de fichiers ne passe pas par le noyau Linux.
 */

#include "draw_function.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MP3_USE_POSIX_FS                                                       \
	1 /* 1 = <dirent.h> direct, 0 = lv_fs (lettre de lecteur) */

#if MP3_USE_POSIX_FS
#include <dirent.h>
#endif

#define SCREEN_W 240
#define SCREEN_H 320

#define WIRE_COLOR_HEX 0x4DA6FF

#define SD_MUSIC_DIR                                                           \
	"/sdcard" /* dossier scanne. En mode lv_fs                                 \
			   * (MP3_USE_POSIX_FS=0), prefixer                                \
			   * par la lettre de lecteur enregistree,                         \
			   * ex: "S:/sdcard". */
#define MAX_TRACKS 64
#define MAX_PATH_LEN                                                           \
	300 /* marge large : d_name peut faire                                     \
		 * jusqu'a 255 caracteres selon la                                     \
		 * libc, + prefixe SD_MUSIC_DIR + '/' */

/* Empilement vertical des elements (haut -> bas), en offset depuis le
 * haut de l'ecran. A ajuster si vous changez la taille d'un element. */
#define UI_TITLE_Y 14
#define UI_PROGRESS_Y 42
#define UI_CONTROLS_Y 84
#define UI_VOLUME_Y 146
#define UI_BROWSE_Y 182
#define UI_BOTTOM_ZONE_Y 226 /* haut de la zone cliquable libre, en bas */

#define UI_UPDATE_PERIOD_MS 500 /* rafraichissement barre/temps */

/* ==================== A IMPLEMENTER (couche audio) ====================
 * Interface avec le pipeline audio reel (codec I2S/DAC + decodeur MP3).
 * Ce fichier LVGL ne gere que l'affichage ; brancher ici votre driver.
 */
extern void audio_play_file(
	const char *path);			/* charge et demarre la lecture d'un fichier */
extern void audio_pause(void);	/* met en pause la lecture en cours */
extern void audio_resume(void); /* reprend apres une pause */
extern void audio_stop(void);	/* arrete completement la lecture */
extern uint32_t
audio_get_position_sec(void); /* position de lecture actuelle, en secondes */
extern uint32_t audio_get_duration_sec(
	void); /* duree totale du morceau charge, en secondes (0 si inconnue) */
extern void audio_set_volume(uint8_t percent); /* regle le volume, 0-100 */
extern uint8_t audio_get_volume(
	void); /* volume actuel, 0-100 (utilise a l'ouverture de l'ecran) */
/* ======================================================================== */

static lv_color_t wire_color;

/* ---------- Etat du lecteur ---------- */

static char track_paths[MAX_TRACKS][MAX_PATH_LEN];
static uint32_t track_count = 0;
static int32_t current_track_idx = -1;
static bool is_playing = false;

static lv_obj_t *title_label;
static lv_obj_t *progress_bar;
static lv_obj_t *time_elapsed_label;
static lv_obj_t *time_total_label;
static lv_obj_t *play_pause_btn;
static lv_obj_t *play_pause_icon;

static lv_obj_t *volume_slider;
static lv_obj_t *volume_icon;
static uint8_t volume_before_mute = 50;

static lv_timer_t *ui_update_timer = NULL;

/* Boite de dialogue de selection (meme pattern que pie_dialog_lvgl.c) */
static lv_obj_t *browse_modal_bg = NULL;
static lv_obj_t *browse_panel = NULL;
static lv_obj_t *browse_list_container = NULL;

/* Zone cliquable libre, en bas de l'ecran : comportement non impose,
 * a brancher via mp3_player_set_bottom_click_cb(). */
typedef void (*mp3_player_bottom_click_cb_t)(void);
static mp3_player_bottom_click_cb_t bottom_click_cb = NULL;

/* ---------- Utilitaires ---------- */

static void format_mmss(uint32_t total_sec, char *buf, size_t buf_len) {
	uint32_t m = total_sec / 60;
	uint32_t s = total_sec % 60;
	snprintf(buf, buf_len, "%02u:%02u", (unsigned)m, (unsigned)s);
}

/* Extrait le nom de fichier (sans le chemin) d'un chemin complet */
static const char *path_basename(const char *path) {
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int has_mp3_extension(const char *fname) {
	size_t len = strlen(fname);
	if (len < 4)
		return 0;
	const char *ext = fname + len - 4;
	return (ext[0] == '.' && (ext[1] == 'm' || ext[1] == 'M') &&
			(ext[2] == 'p' || ext[2] == 'P') && (ext[3] == '3'));
}

/* ---------- Boutons de commande "fil de fer" (contour + symbole) ---------- */

static void wire_btn_style_apply(lv_obj_t *btn, uint16_t size) {
	lv_obj_remove_style_all(btn);
	lv_obj_set_size(btn, size, size);
	lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(btn, 1, 0);
	lv_obj_set_style_border_color(btn, wire_color, 0);
	lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
	lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *wire_icon_btn_create(lv_obj_t *parent, const char *symbol,
									  uint16_t size, lv_event_cb_t cb) {
	lv_obj_t *btn = lv_btn_create(parent);
	wire_btn_style_apply(btn, size);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *lbl = lv_label_create(btn);
	lv_label_set_text(lbl, symbol);
	lv_obj_set_style_text_color(lbl, wire_color, 0);
	lv_obj_center(lbl);
	lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
	return btn;
}

/* ---------- Chargement / changement de morceau ---------- */

static void update_play_pause_icon(void) {
	lv_label_set_text(play_pause_icon,
					  is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void load_and_play_track(int32_t idx) {
	if (idx < 0 || (uint32_t)idx >= track_count)
		return;

	current_track_idx = idx;
	audio_play_file(track_paths[idx]);
	is_playing = true;
	update_play_pause_icon();

	lv_label_set_text(title_label, path_basename(track_paths[idx]));

	uint32_t duration = audio_get_duration_sec();
	lv_bar_set_range(progress_bar, 0, (int32_t)(duration > 0 ? duration : 1));
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

	char buf[8];
	format_mmss(0, buf, sizeof(buf));
	lv_label_set_text(time_elapsed_label, buf);
	format_mmss(duration, buf, sizeof(buf));
	lv_label_set_text(time_total_label, buf);
}

static void play_next_track(void) {
	if (track_count == 0)
		return;
	int32_t next = (current_track_idx + 1) % (int32_t)track_count;
	load_and_play_track(next);
}

static void play_prev_track(void) {
	if (track_count == 0)
		return;
	int32_t prev = current_track_idx - 1;
	if (prev < 0)
		prev = (int32_t)track_count - 1;
	load_and_play_track(prev);
}

/* ---------- Callbacks des controles ---------- */

static void prev_btn_event_cb(lv_event_t *e) {
	LV_UNUSED(e);
	play_prev_track();
}

static void next_btn_event_cb(lv_event_t *e) {
	LV_UNUSED(e);
	play_next_track();
}

static void play_pause_btn_event_cb(lv_event_t *e) {
	LV_UNUSED(e);
	if (current_track_idx < 0)
		return; /* aucun morceau charge */

	if (is_playing) {
		audio_pause();
		is_playing = false;
	} else {
		audio_resume();
		is_playing = true;
	}
	update_play_pause_icon();
}

/* ---------- Mise a jour periodique (barre de progression + temps) ----------
 */

static void ui_update_timer_cb(lv_timer_t *timer) {
	LV_UNUSED(timer);
	if (!is_playing || current_track_idx < 0)
		return;

	uint32_t pos = audio_get_position_sec();
	uint32_t dur = audio_get_duration_sec();

	lv_bar_set_value(progress_bar, (int32_t)pos, LV_ANIM_OFF);

	char buf[8];
	format_mmss(pos, buf, sizeof(buf));
	lv_label_set_text(time_elapsed_label, buf);

	/* Fin de morceau -> passe automatiquement au suivant */
	if (dur > 0 && pos >= dur) {
		play_next_track();
	}
}

/* ---------- Liste de selection (modale, meme pattern que pie_dialog)
 * ---------- */

static void browse_modal_close(void) {
	if (browse_modal_bg == NULL)
		return;
	lv_obj_del(browse_modal_bg);
	browse_modal_bg = NULL;
	browse_panel = NULL;
	browse_list_container = NULL;
}

static void browse_modal_bg_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	if (lv_event_get_target(e) != browse_modal_bg)
		return;
	browse_modal_close();
}

static void browse_close_btn_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	browse_modal_close();
}

static void track_row_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
	load_and_play_track((int32_t)idx);
	browse_modal_close();
}

/* Scanne SD_MUSIC_DIR et remplit track_paths[]. Deux implementations
 * selon MP3_USE_POSIX_FS (voir commentaire en tete de fichier). */
static void scan_sd_music_dir(void) {
	track_count = 0;

#if MP3_USE_POSIX_FS
	DIR *dir = opendir(SD_MUSIC_DIR);
	if (dir == NULL) {
		printf("mp3_player: impossible d'ouvrir %s (opendir a echoue - "
			   "verifier que la carte SD est bien montee a cet endroit)\n",
			   SD_MUSIC_DIR);
		return;
	}

	struct dirent *entry;
	while (track_count < MAX_TRACKS && (entry = readdir(dir)) != NULL) {
		if (!has_mp3_extension(entry->d_name))
			continue;

		int written = snprintf(track_paths[track_count], MAX_PATH_LEN, "%s/%s",
							   SD_MUSIC_DIR, entry->d_name);
		if (written < 0 || (size_t)written >= MAX_PATH_LEN) {
			printf("mp3_player: chemin trop long, fichier ignore: %s\n",
				   entry->d_name);
			continue; /* ne pas incrementer track_count : entree ignoree */
		}
		track_count++;
	}
	closedir(dir);

#else
	lv_fs_dir_t dir;
	lv_fs_res_t res = lv_fs_dir_open(&dir, SD_MUSIC_DIR);
	if (res != LV_FS_RES_OK) {
		printf("mp3_player: impossible d'ouvrir %s (res=%d) - verifier que "
			   "le chemin commence bien par la lettre de lecteur enregistree "
			   "(ex: 'S:/sdcard', pas juste '/sdcard')\n",
			   SD_MUSIC_DIR, (int)res);
		return;
	}

	char fname[96];
	while (track_count < MAX_TRACKS) {
		res = lv_fs_dir_read(&dir, fname);
		if (res != LV_FS_RES_OK || fname[0] == '\0')
			break;
		if (!has_mp3_extension(fname))
			continue;

		int written = snprintf(track_paths[track_count], MAX_PATH_LEN, "%s/%s",
							   SD_MUSIC_DIR, fname);
		if (written < 0 || (size_t)written >= MAX_PATH_LEN) {
			printf("mp3_player: chemin trop long, fichier ignore: %s\n", fname);
			continue;
		}
		track_count++;
	}
	lv_fs_dir_close(&dir);
#endif
}

static void build_track_list_rows(lv_obj_t *list_container) {
	if (track_count == 0) {
		lv_obj_t *empty_lbl = lv_label_create(list_container);
		lv_label_set_text(empty_lbl,
						  "Aucun fichier .mp3 trouve sur\n" SD_MUSIC_DIR);
		lv_obj_set_style_text_color(empty_lbl, wire_color, 0);
		lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_clear_flag(empty_lbl, LV_OBJ_FLAG_CLICKABLE);
		return;
	}

	for (uint32_t i = 0; i < track_count; i++) {
		lv_obj_t *row = lv_obj_create(list_container);
		lv_obj_remove_style_all(row);
		lv_obj_set_size(row, lv_pct(100), 34);
		lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_width(row, 1, 0);
		lv_obj_set_style_border_color(
			row, lv_color_hex(0x224a63),
			0); /* accent assombri, simple separateur */
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_event_cb(row, track_row_event_cb, LV_EVENT_CLICKED,
							(void *)(uintptr_t)i);

		lv_obj_t *icon = lv_label_create(row);
		lv_label_set_text(icon, LV_SYMBOL_AUDIO);
		lv_obj_set_style_text_color(icon, wire_color, 0);
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
		lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

		lv_obj_t *name_lbl = lv_label_create(row);
		lv_label_set_text(name_lbl, path_basename(track_paths[i]));
		lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
		lv_obj_set_width(name_lbl, lv_pct(85));
		lv_obj_set_style_text_color(name_lbl, wire_color, 0);
		lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 24, 0);
		lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE);
	}
}

static void browse_open(lv_obj_t *parent_scr) {
	if (browse_modal_bg != NULL)
		return;

	scan_sd_music_dir();

	browse_modal_bg = lv_obj_create(parent_scr);
	lv_obj_remove_style_all(browse_modal_bg);
	lv_obj_set_size(browse_modal_bg, SCREEN_W, SCREEN_H);
	lv_obj_align(browse_modal_bg, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(browse_modal_bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(browse_modal_bg, LV_OPA_70, 0);
	lv_obj_add_flag(browse_modal_bg, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(browse_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(browse_modal_bg, browse_modal_bg_event_cb,
						LV_EVENT_CLICKED, NULL);
	lv_obj_move_foreground(browse_modal_bg);

	browse_panel = lv_obj_create(browse_modal_bg);
	lv_obj_remove_style_all(browse_panel);
	lv_obj_set_size(browse_panel, 208, 260);
	lv_obj_align(browse_panel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(browse_panel, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(browse_panel, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(browse_panel, 8, 0);
	lv_obj_set_style_border_width(browse_panel, 1, 0);
	lv_obj_set_style_border_color(browse_panel, wire_color, 0);
	lv_obj_set_style_pad_all(browse_panel, 12, 0);
	lv_obj_add_flag(browse_panel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(browse_panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title = lv_label_create(browse_panel);
	lv_label_set_text(title, "Choisir un morceau");
	lv_obj_set_style_text_color(title, wire_color, 0);
	lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

	lv_obj_t *close_btn = wire_icon_btn_create(browse_panel, LV_SYMBOL_CLOSE,
											   22, browse_close_btn_event_cb);
	lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, -2);

	browse_list_container = lv_obj_create(browse_panel);
	lv_obj_remove_style_all(browse_list_container);
	lv_obj_set_size(browse_list_container, lv_pct(100), 200);
	lv_obj_align(browse_list_container, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_flex_flow(browse_list_container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_bg_opa(browse_list_container, LV_OPA_TRANSP, 0);
	lv_obj_set_scroll_dir(browse_list_container, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(browse_list_container, LV_SCROLLBAR_MODE_AUTO);

	build_track_list_rows(browse_list_container);
}

static void browse_btn_event_cb(lv_event_t *e) {
	lv_obj_t *parent_scr =
		lv_obj_get_screen((lv_obj_t *)lv_event_get_target(e));
	browse_open(parent_scr);
}

/* ---------- Reglage du son ---------- */

/* Choisit l'icone haut-parleur selon le niveau (0 = coupe, sinon
 * bas/fort) -- LVGL fournit ces 3 glyphes de base. */
static void update_volume_icon(uint8_t vol) {
	const char *sym;
	if (vol == 0)
		sym = LV_SYMBOL_MUTE;
	else if (vol < 55)
		sym = LV_SYMBOL_VOLUME_MID;
	else
		sym = LV_SYMBOL_VOLUME_MAX;
	lv_label_set_text(volume_icon, sym);
}

static void volume_slider_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
		return;
	uint8_t v = (uint8_t)lv_slider_get_value(volume_slider);
	audio_set_volume(v);
	update_volume_icon(v);
	if (v > 0)
		volume_before_mute = v;
}

/* Clic sur l'icone haut-parleur : bascule mute / dernier volume connu */
static void volume_icon_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;

	uint8_t current = (uint8_t)lv_slider_get_value(volume_slider);
	uint8_t new_vol;

	if (current > 0) {
		volume_before_mute = current;
		new_vol = 0;
	} else {
		new_vol = (volume_before_mute > 0) ? volume_before_mute : 50;
	}

	lv_slider_set_value(volume_slider, new_vol, LV_ANIM_ON);
	audio_set_volume(new_vol);
	update_volume_icon(new_vol);
}

/* ---------- Zone cliquable libre, en bas de l'ecran ----------
 * Aucun comportement impose ici : le callback est enregistre par
 * l'appelant via mp3_player_set_bottom_click_cb(). Exemples d'usage
 * typiques : changer d'ecran/frame, ouvrir un menu, afficher les
 * infos du morceau en cours -- a vous de choisir. */

static void bottom_zone_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	if (bottom_click_cb != NULL) {
		bottom_click_cb();
	}
}

/**
 * @brief Enregistre le callback appele au clic sur la zone basse de
 *        l'ecran du lecteur. Passer NULL pour desactiver.
 */
void mp3_player_set_bottom_click_cb(mp3_player_bottom_click_cb_t cb) {
	bottom_click_cb = cb;
}

/* ---------- Construction de l'ecran lecteur ---------- */

/**
 * @brief Construit l'ecran du lecteur MP3 (style fil de fer 0x4DA6FF).
 * @param scr Ecran ou conteneur parent (ex: lv_scr_act())
 */
void mp3_player_create(lv_obj_t *scr) {
	wire_color = lv_color_hex(WIRE_COLOR_HEX);

	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	/* Titre du morceau (defile si trop long) */
	title_label = lv_label_create(scr);
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_width(title_label, 200);
	lv_label_set_text(title_label, "Aucun morceau");
	lv_obj_set_style_text_color(title_label, wire_color, 0);
	lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, UI_TITLE_Y);
	lv_obj_clear_flag(title_label, LV_OBJ_FLAG_CLICKABLE);

	/* Barre de progression, style contour + remplissage plein */
	progress_bar = lv_bar_create(scr);
	lv_obj_set_size(progress_bar, 200, 6);
	lv_obj_align(progress_bar, LV_ALIGN_TOP_MID, 0, UI_PROGRESS_Y);
	lv_bar_set_range(progress_bar, 0, 1);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_radius(progress_bar, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(progress_bar, 0, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(progress_bar, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(progress_bar, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(progress_bar, wire_color, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(progress_bar, wire_color, LV_PART_INDICATOR);
	lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);

	/* Temps ecoule / duree totale, de part et d'autre de la barre */
	time_elapsed_label = lv_label_create(scr);
	lv_label_set_text(time_elapsed_label, "00:00");
	lv_obj_set_style_text_color(time_elapsed_label, wire_color, 0);
	lv_obj_align_to(time_elapsed_label, progress_bar, LV_ALIGN_OUT_BOTTOM_LEFT,
					0, 6);
	lv_obj_clear_flag(time_elapsed_label, LV_OBJ_FLAG_CLICKABLE);

	time_total_label = lv_label_create(scr);
	lv_label_set_text(time_total_label, "00:00");
	lv_obj_set_style_text_color(time_total_label, wire_color, 0);
	lv_obj_align_to(time_total_label, progress_bar, LV_ALIGN_OUT_BOTTOM_RIGHT,
					0, 6);
	lv_obj_clear_flag(time_total_label, LV_OBJ_FLAG_CLICKABLE);

	/* Controles : precedent / lecture-pause / suivant */
	lv_obj_t *prev_btn =
		wire_icon_btn_create(scr, LV_SYMBOL_PREV, 40, prev_btn_event_cb);
	lv_obj_align(prev_btn, LV_ALIGN_TOP_MID, -60, UI_CONTROLS_Y);

	play_pause_btn =
		wire_icon_btn_create(scr, LV_SYMBOL_PLAY, 52, play_pause_btn_event_cb);
	lv_obj_align(play_pause_btn, LV_ALIGN_TOP_MID, 0,
				 UI_CONTROLS_Y -
					 6); /* centre visuellement (bouton plus grand) */
	play_pause_icon = lv_obj_get_child(play_pause_btn, 0);

	lv_obj_t *next_btn =
		wire_icon_btn_create(scr, LV_SYMBOL_NEXT, 40, next_btn_event_cb);
	lv_obj_align(next_btn, LV_ALIGN_TOP_MID, 60, UI_CONTROLS_Y);

	/* Reglage du son : icone (clic = mute) + slider */
	lv_obj_t *volume_row = lv_obj_create(scr);
	lv_obj_remove_style_all(volume_row);
	lv_obj_set_size(volume_row, 200, 24);
	lv_obj_align(volume_row, LV_ALIGN_TOP_MID, 0, UI_VOLUME_Y);
	lv_obj_set_style_bg_opa(volume_row, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_flow(volume_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(volume_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
						  LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(volume_row, LV_OBJ_FLAG_SCROLLABLE);

	volume_icon = lv_label_create(volume_row);
	lv_label_set_text(volume_icon, LV_SYMBOL_VOLUME_MID);
	lv_obj_set_style_text_color(volume_icon, wire_color, 0);
	lv_obj_add_flag(volume_icon, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(volume_icon, volume_icon_event_cb, LV_EVENT_CLICKED,
						NULL);

	volume_slider = lv_slider_create(volume_row);
	lv_obj_set_height(volume_slider, 6);
	lv_obj_set_flex_grow(volume_slider, 1);
	lv_obj_set_style_pad_left(volume_slider, 10, 0);
	lv_slider_set_range(volume_slider, 0, 100);
	lv_obj_set_style_radius(volume_slider, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(volume_slider, 0, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(volume_slider, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(volume_slider, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(volume_slider, wire_color, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(volume_slider, wire_color, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_bg_color(volume_slider, wire_color, LV_PART_KNOB);
	lv_obj_set_style_radius(volume_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_pad_all(volume_slider, 5,
							 LV_PART_KNOB); /* taille de la poignee */
	lv_obj_add_event_cb(volume_slider, volume_slider_event_cb,
						LV_EVENT_VALUE_CHANGED, NULL);

	uint8_t initial_vol = audio_get_volume();
	lv_slider_set_value(volume_slider, initial_vol, LV_ANIM_OFF);
	if (initial_vol > 0)
		volume_before_mute = initial_vol;
	update_volume_icon(initial_vol);

	/* Bouton "Parcourir" -> ouvre la liste des morceaux de la carte SD */
	lv_obj_t *browse_btn = lv_btn_create(scr);
	lv_obj_remove_style_all(browse_btn);
	lv_obj_set_size(browse_btn, 180, 36);
	lv_obj_align(browse_btn, LV_ALIGN_TOP_MID, 0, UI_BROWSE_Y);
	lv_obj_set_style_bg_opa(browse_btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(browse_btn, 1, 0);
	lv_obj_set_style_border_color(browse_btn, wire_color, 0);
	lv_obj_set_style_radius(browse_btn, 6, 0);
	lv_obj_add_event_cb(browse_btn, browse_btn_event_cb, LV_EVENT_CLICKED,
						NULL);

	lv_obj_t *browse_lbl = lv_label_create(browse_btn);
	lv_label_set_text(browse_lbl, LV_SYMBOL_DIRECTORY "  Parcourir (carte SD)");
	lv_obj_set_style_text_color(browse_lbl, wire_color, 0);
	lv_obj_center(browse_lbl);
	lv_obj_clear_flag(browse_lbl, LV_OBJ_FLAG_CLICKABLE);

	/* Zone cliquable libre, sur le reste de l'ecran en bas -- comportement
	 * a definir par l'appelant via mp3_player_set_bottom_click_cb(). */
	lv_obj_t *bottom_zone = lv_obj_create(scr);
	lv_obj_remove_style_all(bottom_zone);
	lv_obj_set_size(bottom_zone, SCREEN_W, SCREEN_H - UI_BOTTOM_ZONE_Y);
	lv_obj_align(bottom_zone, LV_ALIGN_TOP_MID, 0, UI_BOTTOM_ZONE_Y);
	lv_obj_set_style_bg_opa(bottom_zone, LV_OPA_TRANSP, 0);
	lv_obj_add_flag(bottom_zone, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(bottom_zone, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(bottom_zone, bottom_zone_event_cb, LV_EVENT_CLICKED,
						NULL);

	/* Petit trait discret en haut de la zone, pour signaler qu'elle est
	 * cliquable (purement visuel, ne bloque pas le clic en dessous) */
	lv_obj_t *handle = lv_obj_create(bottom_zone);
	lv_obj_remove_style_all(handle);
	lv_obj_set_size(handle, 36, 3);
	lv_obj_set_style_bg_color(handle, wire_color, 0);
	lv_obj_set_style_bg_opa(handle, LV_OPA_50, 0);
	lv_obj_set_style_radius(handle, 2, 0);
	lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 10);
	lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE);

	ui_update_timer =
		lv_timer_create(ui_update_timer_cb, UI_UPDATE_PERIOD_MS, NULL);
}

/**
 * @brief A appeler si l'ecran du lecteur est detruit, pour stopper le
 *        timer de mise a jour et la lecture en cours.
 */
void mp3_player_destroy(void) {
	if (ui_update_timer != NULL) {
		lv_timer_del(ui_update_timer);
		ui_update_timer = NULL;
	}
	if (is_playing) {
		audio_stop();
		is_playing = false;
	}
	browse_modal_close();
}

static lv_obj_t *player_screen = NULL;

static void on_bottom_zone_clicked(void) {
	// --> libre : changer d'ecran/frame, ouvrir un menu, etc.
	pie_dialog_open(lv_scr_act());
}

void show_player() {

	if (!player_screen) {
		player_screen = lv_obj_create(NULL);
		mp3_player_create(player_screen);
		mp3_player_set_bottom_click_cb(on_bottom_zone_clicked);
	}
	lv_scr_load(player_screen);
}

/* Exemple d'utilisation :
 *
 *   static void on_bottom_zone_clicked(void)
 *   {
 *       // --> libre : changer d'ecran/frame, ouvrir un menu, etc.
 *       // lv_scr_load(next_screen);
 *   }
 *
 *   lv_obj_t *scr = lv_scr_act();
 *   mp3_player_create(scr);
 *   mp3_player_set_bottom_click_cb(on_bottom_zone_clicked);
 *
 * Au clic sur "Parcourir", la liste des .mp3 trouves dans SD_MUSIC_DIR
 * s'affiche ; selectionner une ligne charge et lance la lecture. Les
 * boutons precedent/suivant naviguent dans la liste deja scannee.
 * Le slider regle le volume (0-100) ; cliquer sur l'icone haut-parleur
 * bascule mute / dernier volume connu.
 *
 * ============================================================
 * ESP32-S3 (ESP-IDF) - montage de la carte SD avant utilisation
 * ============================================================
 * mp3_player_create() ne monte PAS la carte SD lui-meme : si /sdcard
 * n'est pas deja monte quand on scanne, opendir() echoue et la liste
 * reste vide. Le montage doit se faire une fois, typiquement dans
 * app_main(), AVANT d'appeler mp3_player_create().
 *
 * Exemple typique (interface SDMMC 4 lignes, cas courant sur les kits
 * Waveshare ESP32-S3 avec slot SD integre -- ADAPTER les GPIO/pins selon
 * le schema exact de votre carte, ils different d'un modele Waveshare a
 * l'autre) :
 *
 *   #include "esp_vfs_fat.h"
 *   #include "driver/sdmmc_host.h"
 *   #include "sdmmc_cmd.h"
 *
 *   void sd_card_mount_example(void)
 *   {
 *       esp_vfs_fat_sdmmc_mount_config_t mount_config = {
 *           .format_if_mount_failed = false,
 *           .max_files = 8,
 *           .allocation_unit_size = 16 * 1024,
 *       };
 *
 *       sdmmc_host_t host = SDMMC_HOST_DEFAULT();
 *       sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
 *       // slot_config.width = 4;           // ou 1 selon le cablage
 *       // slot_config.clk = GPIO_NUM_xx;   // a verifier dans le schema
 *       // slot_config.cmd = GPIO_NUM_xx;   // du kit Waveshare utilise
 *       // slot_config.d0  = GPIO_NUM_xx;
 *
 *       sdmmc_card_t *card;
 *       esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host,
 *                                                &slot_config,
 *                                                &mount_config, &card);
 *       if (ret != ESP_OK) {
 *           printf("Echec montage carte SD (0x%x)\n", ret);
 *           return;
 *       }
 *       sdmmc_card_print_info(stdout, card);
 *   }
 *
 * Certains kits Waveshare utilisent le SD en mode SPI plutot que SDMMC
 * (esp_vfs_fat_sdspi_mount() avec sdspi_host.h) -- se referer au
 * schema/BSP exact du modele pour savoir lequel s'applique, et pour les
 * numeros de GPIO CLK/CMD/D0(/D1-D3) ou MISO/MOSI/CLK/CS selon le cas.
 */
