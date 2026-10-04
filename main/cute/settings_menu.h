#ifndef SETTINGS_MENU_H_
#define SETTINGS_MENU_H_

#include "lvgl.h"

// Écran « Paramètres » : boutons Réveil et Version
void settings_menu_create(void);

// Briques communes aux écrans de paramètres (style fil de fer)
lv_obj_t *settings_create_screen(const char *title);
lv_obj_t *settings_create_button(lv_obj_t *parent, const char *text,
								 lv_event_cb_t cb);

#endif
