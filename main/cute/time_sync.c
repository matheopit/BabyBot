#include "time_sync.h"
#include "app_manager.h"
#include "esp_sntp.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

bool time_sync_is_synced(void) {
	time_t now;
	struct tm timeinfo = {0};

	time(&now);
	localtime_r(&now, &timeinfo);

	return (timeinfo.tm_year > 100); // année > 2000 → OK
}

static void apply_timezone_paris(void) {
	setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
	tzset();
}

// Appelé par SNTP (tâche lwIP) quand l'heure est réellement obtenue, même
// longtemps après le démarrage : on arrête SNTP et on prévient la boucle
// principale, qui coupe le Wi-Fi.
static void on_time_sync(struct timeval *tv) {
	esp_sntp_stop(); // heure obtenue, plus besoin d'interroger le serveur

	time_t now = tv->tv_sec;
	struct tm ti;
	localtime_r(&now, &ti);
	printf("SNTP sync ! Heure Paris: %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min,
		   ti.tm_sec);

	int msg = MSG_TIME_READY;
	xQueueSend(app_msg_queue, &msg, 0);
}

void time_sync_start(void) {
	static bool started = false;
	if (started)
		return;
	started = true;

	apply_timezone_paris();
	esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
	esp_sntp_setservername(0, "pool.ntp.org");
	sntp_set_time_sync_notification_cb(on_time_sync);
	esp_sntp_init();
	printf("Synchronisation NTP lancée\n");
}

void time_sync_stop(void) { esp_sntp_stop(); }
