#pragma once

#include <stdbool.h>

// Mise à jour du firmware depuis les releases GitHub, en passant par la SD :
// 1) fw_update_check_start() (Wi-Fi connecté, heure obtenue) : si le tag de
//    la dernière release (vX.Y.Z) est plus récent que la version courante
//    (version.txt), son asset FW_UPDATE_ASSET est téléchargé dans
//    FW_UPDATE_FILE puis MSG_FW_READY est posté. Le Wi-Fi est coupé ensuite.
// 2) Au démarrage, si FW_UPDATE_FILE existe, fw_update_apply_from_sd() le
//    flashe dans la partition OTA libre, le supprime et redémarre dessus.
//    Un .bin déposé à la main dans FW_UPDATE_FILE est flashé de la même façon.
// Le bootloader annule la mise à jour si le nouveau firmware redémarre avant
// fw_update_mark_valid().

#define FW_UPDATE_REPO "matheopit/BabyBot"
#define FW_UPDATE_ASSET "BabyBot.bin"
#define FW_UPDATE_DIR "/sdcard/update"
#define FW_UPDATE_FILE FW_UPDATE_DIR "/firmware.bin"

// true si un firmware attend d'être flashé sur la SD
bool fw_update_pending(void);
// Écran de mise à jour + flash de FW_UPDATE_FILE, puis redémarrage.
// À appeler depuis app_main après LVGL_Init(), ne retourne pas.
void fw_update_apply_from_sd(void);
// Lance la vérification GitHub dans une tâche (coupe le Wi-Fi à la fin)
void fw_update_check_start(void);
// Le firmware fonctionne : annule le retour automatique à l'ancien
void fw_update_mark_valid(void);
