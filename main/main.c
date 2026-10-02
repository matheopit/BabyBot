#include "BAT_Driver.h"
#include "PCM5101.h"
#include "PWR_Key.h"
#include "SD_MMC.h"
#include "ST7789.h"
#include "Wireless.h"
#include "app_manager.h"
#include "fw_update.h"
#include "time_sync.h"
#include <time.h>
QueueHandle_t app_msg_queue;

// Délai max d'attente de la synchro de l'heure avant d'afficher le robot
// quand même (Wi-Fi absent, wifi.txt manquant, serveur NTP injoignable…)
#define TIME_SYNC_TIMEOUT_MS 20000
// Délai après lequel on renonce à l'heure et on coupe le Wi-Fi pour ne pas
// vider la batterie (le réveil ne fonctionnera pas sans heure)
#define WIFI_GIVE_UP_MS (5 * 60 * 1000)
// Joue startup.mp3 au démarrage (0 = désactivé)
#define PLAY_STARTUP_SOUND 0

// La boucle des pilotes tourne à 10 Hz (bouton marche/arrêt) ; la batterie
// est mesurée une fois sur BAT_UPDATE_EVERY (soit 1 fois par seconde)
#define BAT_UPDATE_EVERY 10

void Driver_Loop(void *parameter) {
	Wireless_Init();
	int bat_tick = 0;
	while (1) {
		if (++bat_tick >= BAT_UPDATE_EVERY) {
			bat_tick = 0;
			if (BAT_Update()) {
				// Pas de LVGL ici : l'état passe par app_msg_queue
				bat_state_t state = BAT_Get_State();
				app_manager_notify(EVENT_BATTERY_STATE, &state);
			}
		}
		PWR_Loop();
		vTaskDelay(pdMS_TO_TICKS(100));
	}
	vTaskDelete(NULL);
}

void Driver_Init(void) {
	PWR_Init();
	BAT_Init();
	Flash_Searching();
	xTaskCreatePinnedToCore(Driver_Loop, "Driver task", 4096, NULL, 3, NULL, 0);
}

void app_main(void) {
	app_msg_queue = xQueueCreate(4, sizeof(int));
	SD_Init();
	LCD_Init();
	Audio_Init();
	LVGL_Init(); // returns the screen object

	// Firmware en attente sur la SD : on le flashe et on redémarre dessus
	if (fw_update_pending())
		fw_update_apply_from_sd();

	/********************* Demo *********************/
	app_manager_init();
	app_manager_choose_frame(FRAME_SPLASH_SCREEN);
	// Dessine le splash avant d'allumer l'écran pour éviter le flash au boot
	lv_refr_now(NULL);
	vTaskDelay(pdMS_TO_TICKS(20)); // fin du dernier transfert DMA
	LCD_Display_On();
#if PLAY_STARTUP_SOUND
	Volume_adjustment(10); // volume fixe pour le son de démarrage
	Play_Music("/sdcard", "startup.mp3");
#endif
	Driver_Init();

	const TickType_t boot_tick = xTaskGetTickCount();

	while (1) {

		int msg;
		if (xQueueReceive(app_msg_queue, &msg, 0)) {
			app_manager_handle_msg(msg);
		}

		// Pas d'heure au bout du délai : on démarre quand même, sans bloquer
		// l'enfant sur le splash. Le Wi-Fi continue d'essayer en fond.
		if (!app_manager_is_started() && xTaskGetTickCount() - boot_tick >
								pdMS_TO_TICKS(TIME_SYNC_TIMEOUT_MS)) {
			printf("Heure non synchronisée après %d s, démarrage quand même\n",
				   TIME_SYNC_TIMEOUT_MS / 1000);
			app_manager_start();
		}

		static bool wifi_given_up = false;
		if (!wifi_given_up && !time_sync_is_synced() &&
			xTaskGetTickCount() - boot_tick > pdMS_TO_TICKS(WIFI_GIVE_UP_MS)) {
			wifi_given_up = true;
			printf("Heure toujours absente après %d min, arrêt du Wi-Fi\n",
				   WIFI_GIVE_UP_MS / 60000);
			time_sync_stop();
			WIFI_Stop();
		}

		wakeup();

		vTaskDelay(pdMS_TO_TICKS(10));
		lv_timer_handler();
		LVGL_Screen_Sleep_Loop();
	}
}
