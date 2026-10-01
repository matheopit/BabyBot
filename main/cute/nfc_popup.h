#pragma once

// Popup affichée quand un tag NFC est posé : « Jouer » ou « Réveil ».
// À appeler uniquement depuis la tâche LVGL (boucle principale).

#include <stdbool.h>

// Ouvre la popup pour le mp3 (ou le répertoire si is_dir) associé au tag.
// Remplace une popup déjà ouverte.
void nfc_popup_open(const char *path, bool is_dir);
// Tag retiré : ferme la popup et arrête la musique (ou playlist) lancée par
// « Jouer »
void nfc_popup_tag_removed(void);
