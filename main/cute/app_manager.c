#include "app_manager.h"
#include "cJSON.h"
#include "draw_function.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lwip/apps/sntp.h"
#include "robot_cute.h"
#include "wakeup_settings.h"
#include <stdio.h>
static const char *TAG = "APP_MANAGER";

// État global
static mood_t g_mood = MOOD_HAPPY;
static alarm_t g_alarm = {7, 15,0x1F, false};

// Widgets LVGL externes
extern lv_obj_t *eyes_canvas;
//extern void eyes_set_state(lv_obj_t *canvas, mood_t mood);
//extern void ui_alarm_update(void);

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
   
	// UI initiale
	//eyes_set_state(eyes_canvas, g_mood);
	//ui_alarm_update();
}

// ===============================
// MOOD
// ===============================
void app_manager_set_mood(mood_t mood) {
	g_mood = mood;
	//eyes_set_state(eyes_canvas, mood);

	ESP_LOGI(TAG, "Humeur mise à jour : %d", mood);
}

// ===============================
// ALARM
// ===============================
void app_manager_set_alarm(int hour, int minute, int days , bool enabled) {
	g_alarm.hour = hour;
	g_alarm.minute = minute;
	g_alarm.enabled = enabled;
    g_alarm.days = days;
	ESP_LOGI(TAG, "Réveil : %02d:%02d (enabled=%d)", hour, minute, enabled);

	//ui_alarm_update();
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
		int hour = cJSON_GetObjectItem(alarm, "hour")->valueint;
		int minute = cJSON_GetObjectItem(alarm, "minute")->valueint;
		bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(alarm, "enabled"));

		app_manager_set_alarm(hour, minute,0, enabled);
	}

	cJSON_Delete(root);
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


alarm_t * getAlarm(){
	return &g_alarm;
}