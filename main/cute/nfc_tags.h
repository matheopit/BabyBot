#pragma once

#include <stdbool.h>
#include <stddef.h>

// Association tag NFC -> musique, lue dans NFC_TAGS_FILE_PATH :
//
// {
//   "tags": [
//     { "uid": "047E3859CE2A81", "path": "/sdcard/histoires/loup.mp3" },
//     { "uid": "8A3F2B11",       "path": "/sdcard/comptines" }
//   ]
// }
//
// "path" est soit un fichier .mp3, soit un répertoire (playlist de ses .mp3).
// Un chemin relatif est pris à partir de /sdcard.
#define NFC_TAGS_FILE_PATH "/sdcard/settings/nfc_tags.json"

// Cherche le chemin associé à uid_hex (UID en hexa, casse ignorée).
// Renvoie true si le tag est connu et que le chemin existe ; *is_dir indique
// alors si c'est un répertoire (playlist) ou un fichier.
bool nfc_tags_lookup(const char *uid_hex, char *path, size_t path_len,
					 bool *is_dir);
