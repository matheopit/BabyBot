/**
 * @file pie_icons.h
 * @brief Declarations des 4 icones "Section 1..4" (badges numerotes,
 *        couleur 0x4DA6FF, generes en C array pour integration binaire).
 *
 * A inclure dans pie_dialog_lvgl.c a la place du texte, une fois
 * icon_section_1.c .. icon_section_4.c ajoutes au projet.
 */
#ifndef PIE_ICONS_H
#define PIE_ICONS_H

#include "lvgl.h"

LV_IMG_DECLARE(icon_section_1);
LV_IMG_DECLARE(icon_section_2);
LV_IMG_DECLARE(icon_section_3);
LV_IMG_DECLARE(icon_section_4);

#endif /* PIE_ICONS_H */
