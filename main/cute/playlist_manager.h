#pragma once

#include <stdbool.h>
#include <stdint.h>

// Gestion des répertoires et des .mp3 de la carte SD, sans aucune dépendance
// à l'IHM :
//   - un explorateur : dossier courant, son contenu (dossiers puis .mp3 triés),
//     navigation dans les sous-dossiers et retour au dossier parent ;
//   - une playlist : liste des .mp3 d'un dossier, morceau courant,
//     précédent / suivant (circulaire).
//
// Comme l'IHM, ce module ne doit être appelé que depuis la tâche LVGL
// (aucun verrou interne).
//
// CARTE SD : deux modes, choisis via PLAYLIST_USE_POSIX_FS.
//   - 1 (par défaut) : accès direct via <dirent.h> (opendir/readdir), chemin
//     POSIX classique, ex "/sdcard".
//   - 0 : API filesystem virtuelle de LVGL (lv_fs_dir_open/read), qui exige un
//     préfixe « lettre de lecteur » enregistré via lv_fs_drv_register (ex:
//     "S:/sdcard"). Nécessaire sur cible bare-metal (STM32 + FatFs).
#define PLAYLIST_USE_POSIX_FS 1

// Racine de l'explorateur (on ne remonte jamais au-dessus). En mode lv_fs
// (PLAYLIST_USE_POSIX_FS=0), préfixer par la lettre de lecteur enregistrée,
// ex: "S:/sdcard".
#define PLAYLIST_ROOT_DIR "/sdcard"

#define PLAYLIST_MAX_TRACKS 64
#define PLAYLIST_MAX_ENTRIES 64	  // dossiers + .mp3 listés par dossier
#define PLAYLIST_MAX_NAME_LEN 128 // nom d'une entrée (sans le chemin)
// Marge large : d_name peut faire jusqu'à 255 caractères selon la libc, plus
// le préfixe PLAYLIST_ROOT_DIR et le '/'
#define PLAYLIST_MAX_PATH_LEN 300

typedef struct {
	char name[PLAYLIST_MAX_NAME_LEN];
	bool is_dir;
} playlist_entry_t;

// ---------- Utilitaires ----------

// Nom de fichier (sans le chemin) d'un chemin complet
const char *playlist_path_basename(const char *path);

// ---------- Explorateur ----------

// Relit le dossier courant. S'il ne peut pas être ouvert (carte changée...),
// revient à la racine. Renvoie false si même la racine est illisible (la liste
// est alors vide).
bool playlist_browse_refresh(void);

// Le dossier courant est-il la racine ?
bool playlist_browse_at_root(void);

// Titre à afficher pour le dossier courant : « Carte SD » à la racine, sinon
// le nom du dossier.
const char *playlist_browse_title(void);

// Contenu du dossier courant (dossiers d'abord, puis .mp3, par ordre
// alphabétique) tel que lu par le dernier playlist_browse_refresh().
uint32_t playlist_browse_count(void);
// NULL si idx est hors limites.
const playlist_entry_t *playlist_browse_entry(uint32_t idx);

// Entre dans le sous-dossier idx puis relit le contenu. Renvoie false (sans
// rien changer) si idx n'est pas un dossier ou si le chemin serait trop long.
bool playlist_browse_enter(uint32_t idx);

// Remonte au dossier parent (sans jamais dépasser la racine) puis relit le
// contenu. Sans effet à la racine.
void playlist_browse_parent(void);

// ---------- Playlist ----------

// La playlist devient la liste des .mp3 du dossier affiché par l'explorateur.
// Renvoie l'index dans la playlist du fichier correspondant à l'entrée
// entry_idx de l'explorateur, ou -1 si ce n'est pas un .mp3 retenu. Le morceau
// courant n'est pas modifié : l'appelant le choisit avec playlist_select().
int32_t playlist_set_from_browse(uint32_t entry_idx);

// Nombre de morceaux de la playlist.
uint32_t playlist_count(void);

// Index du morceau courant, -1 si aucun n'a été sélectionné.
int32_t playlist_current(void);

// Chemin complet du morceau idx, NULL si idx est hors limites.
const char *playlist_track_path(int32_t idx);

// Définit le morceau courant et renvoie son chemin. NULL (sans rien changer)
// si idx est hors limites.
const char *playlist_select(int32_t idx);

// Index du morceau suivant / précédent (circulaire) à partir du morceau
// courant, -1 si la playlist est vide. Ne change pas le morceau courant :
// passer le résultat à playlist_select().
int32_t playlist_next_index(void);
int32_t playlist_prev_index(void);
