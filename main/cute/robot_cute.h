#pragma once

#include "lvgl.h"

void draw_robot();

/* Met à jour les yeux du robot selon l'humeur courante (tâche LVGL uniquement) */
void robot_update_mood(void);
