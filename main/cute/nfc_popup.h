#pragma once

// Popup affichée quand un tag NFC est posé : « Jouer » ou « Réveil ».
// À appeler uniquement depuis la tâche LVGL (boucle principale).

// Ouvre la popup pour le mp3 associé au tag (remplace une popup déjà ouverte)
void nfc_popup_open(const char *path);
// Tag retiré : ferme la popup et arrête la musique lancée par « Jouer »
void nfc_popup_tag_removed(void);
