/**
 * @file playlist_manager.c
 * @brief Explorateur de la carte SD et playlist de .mp3, sans IHM (voir
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

/* Playlist remplie a partir du dossier de l'explorateur quand on lance un
 * fichier. */
static char track_paths[PLAYLIST_MAX_TRACKS][PLAYLIST_MAX_PATH_LEN];
static uint32_t track_count = 0;
static int32_t current_track_idx = -1;

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

/* ---------- Explorateur ---------- */

bool playlist_browse_at_root(void) {
	return strcmp(browse_dir, PLAYLIST_ROOT_DIR) == 0;
}

/* Ajoute une entree (dossier ou .mp3) ; ignore les fichiers caches, le
 * dossier settings a la racine et les noms trop longs */
static void browse_add_entry(const char *name, bool is_dir) {
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

/* Liste le contenu de browse_dir dans browse_entries[]. Deux
 * implementations selon PLAYLIST_USE_POSIX_FS (voir playlist_manager.h).
 * Retourne false si le dossier ne peut pas etre ouvert. */
static bool scan_browse_dir(void) {
	browse_entry_count = 0;

#if PLAYLIST_USE_POSIX_FS
	DIR *dir = opendir(browse_dir);
	if (dir == NULL) {
		printf("playlist_manager: impossible d'ouvrir %s (opendir a echoue - "
			   "verifier que la carte SD est bien montee a cet endroit)\n",
			   browse_dir);
		return false;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
		browse_add_entry(entry->d_name, entry->d_type == DT_DIR);
	closedir(dir);

#else
	lv_fs_dir_t dir;
	lv_fs_res_t res = lv_fs_dir_open(&dir, browse_dir);
	if (res != LV_FS_RES_OK) {
		printf("playlist_manager: impossible d'ouvrir %s (res=%d) - verifier "
			   "que le chemin commence bien par la lettre de lecteur "
			   "enregistree (ex: 'S:/sdcard', pas juste '/sdcard')\n",
			   browse_dir, (int)res);
		return false;
	}

	char fname[256];
	while (1) {
		res = lv_fs_dir_read(&dir, fname);
		if (res != LV_FS_RES_OK || fname[0] == '\0')
			break;
		/* lv_fs prefixe les noms de dossiers par '/' */
		if (fname[0] == '/')
			browse_add_entry(fname + 1, true);
		else
			browse_add_entry(fname, false);
	}
	lv_fs_dir_close(&dir);
#endif

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

/* ---------- Playlist ---------- */

/* La playlist (precedent/suivant) devient la liste des .mp3 du dossier
 * affiche */
int32_t playlist_set_from_browse(uint32_t entry_idx) {
	int32_t play_idx = -1;
	track_count = 0;
	for (uint32_t i = 0;
		 i < browse_entry_count && track_count < PLAYLIST_MAX_TRACKS; i++) {
		if (browse_entries[i].is_dir)
			continue;
		int written = snprintf(track_paths[track_count], PLAYLIST_MAX_PATH_LEN,
							   "%s/%s", browse_dir, browse_entries[i].name);
		if (written < 0 || (size_t)written >= PLAYLIST_MAX_PATH_LEN)
			continue; /* chemin trop long : fichier ignore */
		if (i == entry_idx)
			play_idx = (int32_t)track_count;
		track_count++;
	}
	return play_idx;
}

uint32_t playlist_count(void) { return track_count; }

int32_t playlist_current(void) { return current_track_idx; }

const char *playlist_track_path(int32_t idx) {
	if (idx < 0 || (uint32_t)idx >= track_count)
		return NULL;
	return track_paths[idx];
}

const char *playlist_select(int32_t idx) {
	const char *path = playlist_track_path(idx);
	if (path != NULL)
		current_track_idx = idx;
	return path;
}

int32_t playlist_next_index(void) {
	if (track_count == 0)
		return -1;
	return (current_track_idx + 1) % (int32_t)track_count;
}

int32_t playlist_prev_index(void) {
	if (track_count == 0)
		return -1;
	int32_t prev = current_track_idx - 1;
	if (prev < 0)
		prev = (int32_t)track_count - 1;
	return prev;
}
