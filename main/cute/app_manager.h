#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define COLOR_MAIN 0x4DA6FF
#define MSG_TIME_READY 1
#define ALARM_SOUND_PATH_LEN 128
extern QueueHandle_t app_msg_queue;

typedef enum { MOOD_ANGRY, MOOD_HAPPY, MOOD_TIRED } mood_t;

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
	char sound[ALARM_SOUND_PATH_LEN]; // chemin du mp3 joué au réveil ("" = aucun)
} alarm_t;

typedef enum {
	EVENT_NFC_TAG,
	EVENT_JSON_UPDATE,
	EVENT_ALARM_TRIGGER
} app_event_t;

void app_manager_init(void);
void app_manager_notify(app_event_t event, void *data);
void app_manager_set_mood(mood_t mood);
void app_manager_set_alarm(int hour, int minute, int days, bool enabled);
void app_manager_choose_frame(frame_select_t frame);
void app_manager_setup_time();
alarm_t *getAlarm();
bool set_wakeup_config(void);
