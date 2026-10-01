#pragma once

#include <stdbool.h>

// Synchro de l'heure par SNTP (pool.ntp.org, fuseau Europe/Paris).
// MSG_TIME_READY est posté sur app_msg_queue quand l'heure est obtenue.

// Lance la synchro sans bloquer (appelé à chaque obtention d'IP, SNTP n'est
// lancé qu'une fois)
void time_sync_start(void);
// Abandon de la synchro (heure jamais obtenue) : SNTP arrêté
void time_sync_stop(void);
bool time_sync_is_synced(void);
