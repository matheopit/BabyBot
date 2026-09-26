#include "app_manager.h"
#include "LVGL_Driver.h"
#include "PCM5101.h"
#include "cJSON.h"
#include "draw_function.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lwip/apps/sntp.h"
#include "nfc_popup.h"
#include "robot_cute.h"
#include "wakeup_settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static const char *TAG = "APP_MANAGER";

#define CONFIG_DIR "/sdcard/settings"
#define CONFIG_FILE_PATH CONFIG_DIR "/config.json"
// Musique associée à un tag : NFC_MUSIC_DIR/<UID en hexa>.mp3
#define NFC_MUSIC_DIR "/sdcard"

// État global
static mood_t g_mood = MOOD_NORMAL;
static alarm_t g_alarm = {7, 15, 0x1F, false, "", false};

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
	g_mood = MOOD_NORMAL;
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
	/* Appelée depuis la boucle principale (tâche LVGL) : on peut rafraîchir
	 * l'écran robot directement */
	robot_update_mood();

	ESP_LOGI(TAG, "Humeur mise à jour : %d", mood);
}
mood_t app_manager_get_mood(){
	return g_mood;
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
// Dernier tag posé, écrit par la tâche NFC et lu par la boucle principale
static nfc_tag_t nfc_pending_tag;
static portMUX_TYPE nfc_lock = portMUX_INITIALIZER_UNLOCKED;

// Appelée depuis la tâche NFC : pas de LVGL ici, on passe par app_msg_queue
static void app_manager_post_msg(int msg) {
	if (xQueueSend(app_msg_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
		ESP_LOGW(TAG, "app_msg_queue pleine, message %d perdu", msg);
	}
}

// Tag posé (boucle principale) : ouvre la popup si NFC_MUSIC_DIR/<UID>.mp3
// existe
static void app_manager_handle_nfc(void) {
	nfc_tag_t tag;
	taskENTER_CRITICAL(&nfc_lock);
	tag = nfc_pending_tag;
	taskEXIT_CRITICAL(&nfc_lock);

	char uid_hex[NFC_UID_MAX_LEN * 2 + 1] = "";
	for (int i = 0; i < tag.len && i < NFC_UID_MAX_LEN; i++)
		sprintf(uid_hex + 2 * i, "%02X", tag.uid[i]);

	char path[64];
	snprintf(path, sizeof(path), "%s/%s.mp3", NFC_MUSIC_DIR, uid_hex);

	struct stat st;
	if (stat(path, &st) != 0) {
		ESP_LOGW(TAG, "Tag %s inconnu : copier un mp3 sous %s", uid_hex, path);
		return;
	}

	ESP_LOGI(TAG, "Tag %s : %s", uid_hex, path);
	nfc_popup_open(path);
}

// Messages NFC reçus par la boucle principale (tâche LVGL)
void app_manager_handle_msg(int msg) {
	switch (msg) {
	case MSG_NFC_TAG:
		app_manager_handle_nfc();
		break;
	case MSG_NFC_TAG_REMOVED:
		ESP_LOGI(TAG, "Tag retiré");
		nfc_popup_tag_removed();
		break;
	default:
		break;
	}
}

// ===============================
// EVENT DISPATCHER
// ===============================
void app_manager_notify(app_event_t event, void *data) {
	switch (event) {

	case EVENT_NFC_TAG:
		taskENTER_CRITICAL(&nfc_lock);
		nfc_pending_tag = *(const nfc_tag_t *)data;
		taskEXIT_CRITICAL(&nfc_lock);
		app_manager_post_msg(MSG_NFC_TAG);
		break;

	case EVENT_NFC_TAG_REMOVED:
		app_manager_post_msg(MSG_NFC_TAG_REMOVED);
		break;

	case EVENT_JSON_UPDATE:
		app_manager_parse_json((char *)data);
		break;

	case EVENT_ALARM_TRIGGER:
		ESP_LOGI(TAG, "Réveil déclenché !");
		app_manager_set_mood(MOOD_TIRED);
		break;
	}
}

void app_manager_choose_frame(frame_select_t frame) {

	getAlarm()->in_settings = false;
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

bool app_manager_time_is_synced(void) {
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

	while (!app_manager_time_is_synced() && retry < max_retry) {
		printf("SNTP non sync...\n");
		vTaskDelay(pdMS_TO_TICKS(500));
		retry++;
	}

	if (app_manager_time_is_synced()) {
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
	if (app_manager_time_is_synced()) {
		sntp_stop(); // heure obtenue, plus besoin d'interroger le serveur
	}
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

// Déclenche le réveil à l'heure et aux jours configurés (une fois par minute)
void wakeup(void) {
	static int last_trigger = -1; // jour de l'année * 1440 + minute du jour

	alarm_t *alarm = getAlarm();
	if (!alarm->enabled || alarm->in_settings)
		return;

	time_t now;
	struct tm ti;
	time(&now);
	localtime_r(&now, &ti);
	if (ti.tm_year <= 100) // heure pas encore synchronisée
		return;

	int day_bit = (ti.tm_wday + 6) % 7; // tm_wday : 0 = dimanche -> bit 0 = lundi
	if (!(alarm->days & (1 << day_bit)))
		return;
	if (ti.tm_hour != alarm->hour || ti.tm_min != alarm->minute)
		return;

	int stamp = ti.tm_yday * 1440 + ti.tm_hour * 60 + ti.tm_min;
	if (stamp == last_trigger)
		return;
	last_trigger = stamp;

	printf("Réveil ! %02d:%02d\n", ti.tm_hour, ti.tm_min);
	LVGL_Screen_Wake();
	app_manager_notify(EVENT_ALARM_TRIGGER, NULL);
	if (alarm->sound[0] != '\0') {
		Play_Music_ex(alarm->sound);
	}
}