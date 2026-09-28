#pragma once

#include "lvgl.h"

void draw_robot();

/* Met à jour les yeux du robot selon l'humeur courante (tâche LVGL uniquement) */
void robot_update_mood(void);

/* Affiche ou masque le bouton « Arrêter le réveil » selon l'état du réveil
 * (tâche LVGL uniquement) */
void robot_alarm_update(void);
