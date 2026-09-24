#include "app_manager.h"
#include "cJSON.h"
#include "draw_function.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lwip/apps/sntp.h"
#include "robot_cute.h"
#include "wakeup_settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static const char *TAG = "APP_MANAGER";

#define CONFIG_DIR "/sdcard/data"
#define CONFIG_FILE_PATH CONFIG_DIR "/config.json"

// État global
static mood_t g_mood = MOOD_HAPPY;
static alarm_t g_alarm = {7, 15, 0x1F, false, ""};

static void app_manager_parse_json(const char *json);
static void app_manager_load_config(void);

// Widgets LVGL externes
extern lv_obj_t *eyes_canvas;
// extern void eyes_set_state(lv_obj_t *canvas, mood_t mood);
// extern void ui_alarm_update(void);

// ===============================
// INIT
// ===============================
void app_manager_init(void) {
	ESP_LOGI(TAG, "Initialisation App Manager");

	// État par défaut
	g_mood = MOOD_HAPPY;
	g_alarm.hour = 7;
	g_alarm.minute = 30;
	g_alarm.days = 0x1F;
	g_alarm.enabled = false;

	// Répertoire de config sur la carte SD
	struct stat st;
	if (stat(CONFIG_DIR, &st) != 0) {
		if (mkdir(CONFIG_DIR, 0775) == 0) {
			ESP_LOGI(TAG, "Répertoire %s créé", CONFIG_DIR);
		} else {
			ESP_LOGE(TAG, "Impossible de créer %s : %s", CONFIG_DIR,
					 strerror(errno));
		}
	}

	// Réglages sauvegardés (écrase les valeurs par défaut si le fichier existe)
	app_manager_load_config();

	// UI initiale
	// eyes_set_state(eyes_canvas, g_mood);
	// ui_alarm_update();
}

// ===============================
// MOOD
// ===============================
void app_manager_set_mood(mood_t mood) {
	g_mood = mood;
	// eyes_set_state(eyes_canvas, mood);

	ESP_LOGI(TAG, "Humeur mise à jour : %d", mood);
}

// ===============================
// ALARM
// ===============================
void app_manager_set_alarm(int hour, int minute, int days, bool enabled) {
	g_alarm.hour = hour;
	g_alarm.minute = minute;
	g_alarm.enabled = enabled;
	g_alarm.days = days;
	ESP_LOGI(TAG, "Réveil : %02d:%02d (enabled=%d)", hour, minute, enabled);

	// ui_alarm_update();
}

// ===============================
// JSON PARSER
// ===============================
static void app_manager_parse_json(const char *json) {
	cJSON *root = cJSON_Parse(json);
	if (!root) {
		ESP_LOGE(TAG, "JSON invalide");
		return;
	}

	// Mood
	cJSON *mood = cJSON_GetObjectItem(root, "mood");
	if (mood && cJSON_IsString(mood)) {
		if (!strcmp(mood->valuestring, "angry"))
			app_manager_set_mood(MOOD_ANGRY);
		if (!strcmp(mood->valuestring, "happy"))
			app_manager_set_mood(MOOD_HAPPY);
		if (!strcmp(mood->valuestring, "tired"))
			app_manager_set_mood(MOOD_TIRED);
	}

	// Alarm
	cJSON *alarm = cJSON_GetObjectItem(root, "alarm");
	if (alarm) {
		cJSON *hour = cJSON_GetObjectItem(alarm, "hour");
		cJSON *minute = cJSON_GetObjectItem(alarm, "minute");
		if (cJSON_IsNumber(hour) && cJSON_IsNumber(minute)) {
			bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(alarm, "enabled"));
			cJSON *days = cJSON_GetObjectItem(alarm, "days");
			int days_mask = cJSON_IsNumber(days) ? (days->valueint & 0x7F) : 0;

			app_manager_set_alarm(hour->valueint, minute->valueint, days_mask,
								  enabled);

			cJSON *sound = cJSON_GetObjectItem(alarm, "sound");
			if (cJSON_IsString(sound)) {
				strncpy(g_alarm.sound, sound->valuestring,
						sizeof(g_alarm.sound) - 1);
				g_alarm.sound[sizeof(g_alarm.sound) - 1] = '\0';
			}
		} else {
			ESP_LOGE(TAG, "Alarme invalide");
		}
	}

	cJSON_Delete(root);
}

// ===============================
// CHARGEMENT CONFIG
// ===============================
static void app_manager_load_config(void) {
	FILE *f = fopen(CONFIG_FILE_PATH, "r");
	if (!f) {
		ESP_LOGI(TAG, "Pas de %s, valeurs par défaut", CONFIG_FILE_PATH);
		return;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) {
		ESP_LOGE(TAG, "%s vide", CONFIG_FILE_PATH);
		fclose(f);
		return;
	}

	char *buffer = malloc(size + 1);
	if (!buffer) {
		ESP_LOGE(TAG, "malloc impossible");
		fclose(f);
		return;
	}

	size_t len = fread(buffer, 1, size, f);
	buffer[len] = '\0';
	fclose(f);

	app_manager_parse_json(buffer);
	free(buffer);
}

// ===============================
// NFC EVENT
// ===============================
static void app_manager_handle_nfc(uint8_t *uid, int uid_len) {
	ESP_LOGI(TAG, "NFC UID détecté :");
	for (int i = 0; i < uid_len; i++)
		ESP_LOGI(TAG, "%02X ", uid[i]);
	ESP_LOGI(TAG, "\n");

	// Exemple : carte spéciale → humeur joyeuse
	if (uid_len == 4 && uid[0] == 0xDE && uid[1] == 0xAD) {
		app_manager_set_mood(MOOD_HAPPY);
	} else {
		app_manager_set_mood(MOOD_ANGRY);
	}
}

// ===============================
// EVENT DISPATCHER
// ===============================
void app_manager_notify(app_event_t event, void *data) {
	switch (event) {

	case EVENT_NFC_TAG:
		app_manager_handle_nfc((uint8_t *)data, 4);
		break;

	case EVENT_JSON_UPDATE:
		app_manager_parse_json((char *)data);
		break;

	case EVENT_ALARM_TRIGGER:
		ESP_LOGI(TAG, "Réveil déclenché !");
		app_manager_set_mood(MOOD_ANGRY);
		break;
	}
}

void app_manager_choose_frame(frame_select_t frame) {

	switch (frame) {
	case FRAME_SMILE:
		draw_robot();
		break;
	case FRAME_WAKEUP_SETTINGS:
		ui_alarm_screen_create();
		break;
	case FRAME_HORLOGE:
		clock_create();
		break;
	case FRAME_MUSIC:
		show_player();
		break;
	case FRAME_SMILE_SETTINGS:
		break;
	case FRAME_SPLASH_SCREEN:
		babybot_splash_create();
		break;
	default:
		break;
	}
}

static bool sntp_is_synced(void) {
	time_t now;
	struct tm timeinfo = {0};

	time(&now);
	localtime_r(&now, &timeinfo);

	return (timeinfo.tm_year > 100); // année > 2000 → OK
}

static void apply_timezone_paris(void) {
	setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
	tzset();
	vTaskDelay(pdMS_TO_TICKS(200));
}

static void wait_for_sntp_sync(void) {
	int retry = 0;
	const int max_retry = 10;

	while (!sntp_is_synced() && retry < max_retry) {
		printf("SNTP non sync...\n");
		vTaskDelay(pdMS_TO_TICKS(500));
		retry++;
	}

	if (sntp_is_synced()) {
		printf("SNTP sync !\n");
		apply_timezone_paris();

		time_t now;
		struct tm ti;
		time(&now);
		localtime_r(&now, &ti);

		printf("Heure Paris: %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min,
			   ti.tm_sec);
	} else {
		printf("SNTP échec de synchro\n");
	}
}

void app_manager_setup_time() {
	/* --- Lancer SNTP --- */
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_init();
	printf("Synchronisation NTP lancée\n");
	wait_for_sntp_sync();
	int msg = MSG_TIME_READY;
	xQueueSend(app_msg_queue, &msg, 0);
}

alarm_t *getAlarm() { return &g_alarm; }

// ===============================
// SAUVEGARDE CONFIG
// ===============================
bool set_wakeup_config(void) {
	cJSON *root = cJSON_CreateObject();
	if (!root) {
		ESP_LOGE(TAG, "cJSON_CreateObject impossible");
		return false;
	}

	cJSON *alarm = cJSON_AddObjectToObject(root, "alarm");
	if (!alarm) {
		ESP_LOGE(TAG, "cJSON_AddObjectToObject impossible");
		cJSON_Delete(root);
		return false;
	}
	cJSON_AddNumberToObject(alarm, "hour", g_alarm.hour);
	cJSON_AddNumberToObject(alarm, "minute", g_alarm.minute);
	cJSON_AddNumberToObject(alarm, "days", g_alarm.days & 0x7F); // bit 0 = lundi
	cJSON_AddBoolToObject(alarm, "enabled", g_alarm.enabled);
	cJSON_AddStringToObject(alarm, "sound", g_alarm.sound);

	char *json = cJSON_Print(root);
	cJSON_Delete(root);
	if (!json) {
		ESP_LOGE(TAG, "cJSON_Print impossible");
		return false;
	}

	FILE *f = fopen(CONFIG_FILE_PATH, "w");
	if (!f) {
		ESP_LOGE(TAG, "Impossible d'ouvrir %s", CONFIG_FILE_PATH);
		cJSON_free(json);
		return false;
	}

	size_t len = strlen(json);
	bool ok = (fwrite(json, 1, len, f) == len);
	fclose(f);
	cJSON_free(json);

	if (!ok) {
		ESP_LOGE(TAG, "Erreur d'écriture dans %s", CONFIG_FILE_PATH);
		return false;
	}

	ESP_LOGI(TAG, "Réveil sauvegardé dans %s", CONFIG_FILE_PATH);
	return true;
}
