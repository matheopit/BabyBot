#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define COLOR_MAIN 0x4DA6FF
#define MSG_TIME_READY 1
#define MSG_NFC_TAG 2		 // tag posé (voir app_manager_handle_msg)
#define MSG_NFC_TAG_REMOVED 3 // tag retiré
#define MSG_FW_READY 4 // nouveau firmware téléchargé sur la SD
#define MSG_BAT_OK 5   // batterie OK (ou rechargée)
#define MSG_BAT_LOW 6  // batterie faible
#define MSG_BAT_CRITICAL 7 // batterie critique
#define ALARM_SOUND_PATH_LEN 128
#define VOLUME_DEFAULT 10 // volume sans config.json (0-100)
extern QueueHandle_t app_msg_queue;

typedef enum { MOOD_NORMAL, MOOD_ANGRY, MOOD_HAPPY, MOOD_TIRED, MOOD_SAD } mood_t;

typedef enum {
	FRAME_SMILE,
	FRAME_WAKEUP_SETTINGS,
	FRAME_MUSIC,
	FRAME_HORLOGE,
	FRAME_SMILE_SETTINGS,
	FRAME_SPLASH_SCREEN
} frame_select_t;

typedef struct {
	int hour;
	int minute;
	int days; // bitmask : bit 0 = lundi, bit 1 = mardi, ... bit 6 = dimanche
	bool enabled;
	char sound[ALARM_SOUND_PATH_LEN]; // chemin du mp3 joué au réveil ("" =
									  // aucun)
	bool in_settings; // true tant que l'utilisateur règle le réveil
} alarm_t;

#define NFC_UID_MAX_LEN 7

typedef struct {
	uint8_t uid[NFC_UID_MAX_LEN];
	uint8_t len; // 4 ou 7 octets
} nfc_tag_t;

typedef enum {
	EVENT_NFC_TAG,		   // data : nfc_tag_t* du tag posé (tâche NFC)
	EVENT_NFC_TAG_REMOVED, // data : NULL (tâche NFC)
	EVENT_JSON_UPDATE,
	EVENT_ALARM_TRIGGER,
	EVENT_BATTERY_STATE // data : bat_state_t* du nouvel état (tâche pilotes)
} app_event_t;

void app_manager_init(void);
void app_manager_notify(app_event_t event, void *data);
void app_manager_handle_msg(int msg);
void app_manager_start(void);
bool app_manager_is_started(void);
void app_manager_set_mood(mood_t mood);
mood_t app_manager_get_mood(void);
/* Batterie basse : force les yeux fatigués (tâche LVGL uniquement) */
void app_manager_set_battery_low(bool low);
/* Yeux souriants pendant la musique : à appeler à chaque tour de la boucle
 * principale (tâche LVGL uniquement) */
void app_manager_music_check(void);
mood_t app_manager_get_mood();
void app_manager_set_alarm(int hour, int minute, int days, bool enabled);
void app_manager_choose_frame(frame_select_t frame);
alarm_t *getAlarm();
// Écrit config.json : réveil + volume
bool set_wakeup_config(void);
uint8_t app_manager_get_volume(void);
void app_manager_set_volume(uint8_t volume, bool save);
void wakeup(void);
bool app_manager_alarm_ringing(void);
void app_manager_alarm_stop(void);
