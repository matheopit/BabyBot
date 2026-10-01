/* ==================== A IMPLEMENTER (couche audio) ====================
 * Interface avec le pipeline audio reel (codec I2S/DAC + decodeur MP3).
 * Ce fichier LVGL ne gere que l'affichage ; brancher ici votre driver.
 */

#include "PCM5101.h"

void audio_play_file(const char *fileName) { Play_Music_ex(fileName); }
/* met en pause la lecture en cours */
void audio_pause(void) { Music_pause(); }
/* reprend apres une pause, false s'il n'y avait rien a reprendre */
bool audio_resume(void) { return Music_resume(); }
/* arrete completement la lecture */
void audio_stop(void) { Music_stop(); }
/* position de lecture actuelle, en secondes */
uint32_t audio_get_position_sec(void) { return Music_Elapsed(); }
/* duree totale du morceau charge, en secondes (0 si inconnue) */
uint32_t audio_get_duration_sec(void) { return Music_Duration(); }

void audio_set_volume(uint8_t percent) {
	Volume_adjustment(percent);
} /* regle le volume, 0-100 */
uint8_t audio_get_volume(void) { return Volume; }

/* true quand le morceau en cours est termine (ou arrete) */
bool audio_is_finished(void) { return Music_Finished(); }
/* identifiant du dernier morceau lance, quel que soit le module qui l'a lance
 * (change a chaque nouveau morceau) */
uint32_t audio_get_track_id(void) { return Music_Track_Id(); }
/* chemin du dernier morceau lance, "" si aucun */
const char *audio_get_current_path(void) { return Music_Current_Path(); }
