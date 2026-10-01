/**
 * @file playlist_manager.c
 * @brief Explorateur de la carte SD et playlists de .mp3, sans IHM (voir
 *        playlist_manager.h). Extrait de mp3_player_lvgl.c.
 */

#include "playlist_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if PLAYLIST_USE_POSIX_FS
#include <dirent.h>
#else
#include "lvgl.h"
#endif

/* ---------- Etat ---------- */

/* Explorateur : dossier affiche (garde entre deux ouvertures) et son
 * contenu. */
static char browse_dir[PLAYLIST_MAX_PATH_LEN] = PLAYLIST_ROOT_DIR;
static playlist_entry_t browse_entries[PLAYLIST_MAX_ENTRIES];
static uint32_t browse_entry_count = 0;

/* ---------- Utilitaires ---------- */

/* Extrait le nom de fichier (sans le chemin) d'un chemin complet */
const char *playlist_path_basename(const char *path) {
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

/* Appelee pour chaque entree d'un dossier (hors "." et "..") */
typedef void (*dir_entry_cb_t)(const char *name, bool is_dir, void *ctx);

/* Parcourt dir_path et appelle cb pour chaque entree. Deux implementations
 * selon PLAYLIST_USE_POSIX_FS (voir playlist_manager.h). Retourne false si le
 * dossier ne peut pas etre ouvert. */
static bool for_each_dir_entry(const char *dir_path, dir_entry_cb_t cb,
							   void *ctx) {
#if PLAYLIST_USE_POSIX_FS
	DIR *dir = opendir(dir_path);
	if (dir == NULL) {
		printf("playlist_manager: impossible d'ouvrir %s (opendir a echoue - "
			   "verifier que la carte SD est bien montee a cet endroit)\n",
			   dir_path);
		return false;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
		cb(entry->d_name, entry->d_type == DT_DIR, ctx);
	closedir(dir);

#else
	lv_fs_dir_t dir;
	lv_fs_res_t res = lv_fs_dir_open(&dir, dir_path);
	if (res != LV_FS_RES_OK) {
		printf("playlist_manager: impossible d'ouvrir %s (res=%d) - verifier "
			   "que le chemin commence bien par la lettre de lecteur "
			   "enregistree (ex: 'S:/sdcard', pas juste '/sdcard')\n",
			   dir_path, (int)res);
		return false;
	}

	char fname[256];
	while (1) {
		res = lv_fs_dir_read(&dir, fname);
		if (res != LV_FS_RES_OK || fname[0] == '\0')
			break;
		/* lv_fs prefixe les noms de dossiers par '/' */
		if (fname[0] == '/')
			cb(fname + 1, true, ctx);
		else
			cb(fname, false, ctx);
	}
	lv_fs_dir_close(&dir);
#endif
	return true;
}

/* ---------- Explorateur ---------- */

bool playlist_browse_at_root(void) {
	return strcmp(browse_dir, PLAYLIST_ROOT_DIR) == 0;
}

/* Ajoute une entree (dossier ou .mp3) ; ignore les fichiers caches, le
 * dossier settings a la racine et les noms trop longs */
static void browse_add_entry(const char *name, bool is_dir, void *ctx) {
	(void)ctx;
	if (browse_entry_count >= PLAYLIST_MAX_ENTRIES)
		return;
	if (name[0] == '.' || strcmp(name, "System Volume Information") == 0)
		return;
	if (!is_dir && !has_mp3_extension(name))
		return;
	/* Dossier de configuration (config.json...), pas de musique dedans */
	if (is_dir && playlist_browse_at_root() &&
		strcasecmp(name, "settings") == 0)
		return;
	if (strlen(name) >= PLAYLIST_MAX_NAME_LEN) {
		printf("playlist_manager: nom trop long, ignore: %s\n", name);
		return;
	}
	strcpy(browse_entries[browse_entry_count].name, name);
	browse_entries[browse_entry_count].is_dir = is_dir;
	browse_entry_count++;
}

/* Dossiers d'abord, puis fichiers, chacun par ordre alphabetique */
static int browse_entry_cmp(const void *a, const void *b) {
	const playlist_entry_t *ea = a;
	const playlist_entry_t *eb = b;
	if (ea->is_dir != eb->is_dir)
		return ea->is_dir ? -1 : 1;
	return strcasecmp(ea->name, eb->name);
}

/* Liste le contenu de browse_dir dans browse_entries[]. Retourne false si le
 * dossier ne peut pas etre ouvert. */
static bool scan_browse_dir(void) {
	browse_entry_count = 0;
	if (!for_each_dir_entry(browse_dir, browse_add_entry, NULL))
		return false;
	qsort(browse_entries, browse_entry_count, sizeof(browse_entries[0]),
		  browse_entry_cmp);
	return true;
}

/* Relit le dossier courant. Si le dossier n'existe plus (carte changee...),
 * on revient a la racine. */
bool playlist_browse_refresh(void) {
	if (scan_browse_dir())
		return true;
	if (playlist_browse_at_root())
		return false;
	strcpy(browse_dir, PLAYLIST_ROOT_DIR);
	return scan_browse_dir();
}

const char *playlist_browse_title(void) {
	if (playlist_browse_at_root())
		return "Carte SD";
	return playlist_path_basename(browse_dir);
}

uint32_t playlist_browse_count(void) { return browse_entry_count; }

const playlist_entry_t *playlist_browse_entry(uint32_t idx) {
	if (idx >= browse_entry_count)
		return NULL;
	return &browse_entries[idx];
}

bool playlist_browse_enter(uint32_t idx) {
	if (idx >= browse_entry_count || !browse_entries[idx].is_dir)
		return false;

	size_t len = strlen(browse_dir);
	if (len + 1 + strlen(browse_entries[idx].name) >= PLAYLIST_MAX_PATH_LEN) {
		printf("playlist_manager: chemin trop long: %s/%s\n", browse_dir,
			   browse_entries[idx].name);
		return false;
	}
	snprintf(browse_dir + len, PLAYLIST_MAX_PATH_LEN - len, "/%s",
			 browse_entries[idx].name);
	playlist_browse_refresh();
	return true;
}

void playlist_browse_parent(void) {
	if (playlist_browse_at_root())
		return;
	char *slash = strrchr(browse_dir, '/');
	if (slash != NULL)
		*slash = '\0';
	/* ne jamais remonter au-dessus de PLAYLIST_ROOT_DIR */
	if (strlen(browse_dir) < strlen(PLAYLIST_ROOT_DIR))
		strcpy(browse_dir, PLAYLIST_ROOT_DIR);
	playlist_browse_refresh();
}

/* ---------- Playlists ---------- */

static playlist_t active_playlist = PLAYLIST_INITIALIZER;

playlist_t *playlist_active(void) { return &active_playlist; }

void playlist_clear(playlist_t *pl) {
	pl->count = 0;
	pl->current = -1;
}

bool playlist_set_single(playlist_t *pl, const char *path) {
	playlist_clear(pl);
	int written = snprintf(pl->paths[0], PLAYLIST_MAX_PATH_LEN, "%s", path);
	if (written < 0 || (size_t)written >= PLAYLIST_MAX_PATH_LEN)
		return false;
	pl->count = 1;
	pl->current = 0;
	return true;
}

/* La playlist (precedent/suivant) devient la liste des .mp3 du dossier
 * affiche */
int32_t playlist_set_from_browse(playlist_t *pl, uint32_t entry_idx) {
	int32_t play_idx = -1;
	pl->count = 0;
	for (uint32_t i = 0;
		 i < browse_entry_count && pl->count < PLAYLIST_MAX_TRACKS; i++) {
		if (browse_entries[i].is_dir)
			continue;
		int written = snprintf(pl->paths[pl->count], PLAYLIST_MAX_PATH_LEN,
							   "%s/%s", browse_dir, browse_entries[i].name);
		if (written < 0 || (size_t)written >= PLAYLIST_MAX_PATH_LEN)
			continue; /* chemin trop long : fichier ignore */
		if (i == entry_idx)
			play_idx = (int32_t)pl->count;
		pl->count++;
	}
	return play_idx;
}

typedef struct {
	playlist_t *pl;
	const char *dir_path;
} load_dir_ctx_t;

/* Ajoute un .mp3 a la playlist ; ignore les fichiers caches, les dossiers et
 * les chemins trop longs */
static void load_dir_add_entry(const char *name, bool is_dir, void *ctx_) {
	load_dir_ctx_t *ctx = ctx_;
	playlist_t *pl = ctx->pl;
	if (is_dir || pl->count >= PLAYLIST_MAX_TRACKS)
		return;
	if (name[0] == '.' || !has_mp3_extension(name))
		return;
	int written = snprintf(pl->paths[pl->count], PLAYLIST_MAX_PATH_LEN, "%s/%s",
						   ctx->dir_path, name);
	if (written < 0 || (size_t)written >= PLAYLIST_MAX_PATH_LEN) {
		printf("playlist_manager: chemin trop long, ignore: %s\n", name);
		return;
	}
	pl->count++;
}

static int track_path_cmp(const void *a, const void *b) {
	return strcasecmp((const char *)a, (const char *)b);
}

bool playlist_load_dir(playlist_t *pl, const char *dir_path) {
	playlist_clear(pl);
	load_dir_ctx_t ctx = {.pl = pl, .dir_path = dir_path};
	if (!for_each_dir_entry(dir_path, load_dir_add_entry, &ctx)) {
		pl->count = 0;
		return false;
	}
	qsort(pl->paths, pl->count, sizeof(pl->paths[0]), track_path_cmp);
	return true;
}

uint32_t playlist_count(const playlist_t *pl) { return pl->count; }

int32_t playlist_current(const playlist_t *pl) { return pl->current; }

const char *playlist_track_path(const playlist_t *pl, int32_t idx) {
	if (idx < 0 || (uint32_t)idx >= pl->count)
		return NULL;
	return pl->paths[idx];
}

const char *playlist_select(playlist_t *pl, int32_t idx) {
	const char *path = playlist_track_path(pl, idx);
	if (path != NULL)
		pl->current = idx;
	return path;
}

int32_t playlist_next_index(const playlist_t *pl) {
	if (pl->count == 0)
		return -1;
	return (pl->current + 1) % (int32_t)pl->count;
}

int32_t playlist_prev_index(const playlist_t *pl) {
	if (pl->count == 0)
		return -1;
	int32_t prev = pl->current - 1;
	if (prev < 0)
		prev = (int32_t)pl->count - 1;
	return prev;
}
