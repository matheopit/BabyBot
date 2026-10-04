#pragma once

// Écran de progression du flash du firmware (voir fw_update.h)

// Construit l'écran et allume l'afficheur. Tâche LVGL uniquement.
void fw_update_lvgl_show(void);
// Boucle LVGL jusqu'au redémarrage, ne retourne pas. Tâche LVGL uniquement.
void fw_update_lvgl_loop(void);

// Appelables depuis n'importe quelle tâche (pas d'appel lv_*) : l'écran les
// relit périodiquement
void fw_update_lvgl_set_percent(int percent);
void fw_update_lvgl_set_error(const char *error);
