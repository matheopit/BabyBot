#include "BAT_Driver.h"
#include "PCM5101.h"
#include "PWR_Key.h"
#include "SD_MMC.h"
#include "ST7789.h"
#include "Wireless.h"
#include "app_manager.h"
#include "ntag_read.h"
#include <time.h>
QueueHandle_t app_msg_queue;

// Délai max d'attente de la synchro de l'heure avant d'afficher le robot
// quand même (Wi-Fi absent, wifi.txt manquant, serveur NTP injoignable…)
#define TIME_SYNC_TIMEOUT_MS 20000

void Driver_Loop(void *parameter) {
	Wireless_Init();
	while (1) {
		BAT_Get_Volts();
		PWR_Loop();
		vTaskDelay(pdMS_TO_TICKS(100));
	}
	vTaskDelete(NULL);
}

static void nfc_task(void *parameter) {
	while (1) {
		init_nfc();
		while (1) {
			nfc_loop();
		}
	}
}

// Quitte le splash : écran robot + démarrage de la lecture NFC (une seule fois)
static bool app_started = false;
static void app_start(void) {
	if (app_started)
		return;
	app_started = true;
	app_manager_choose_frame(FRAME_SMILE);
	xTaskCreatePinnedToCore(nfc_task, "NFC task", 4096, NULL, 4, NULL, 1);
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

	/********************* Demo *********************/
	app_manager_init();
	app_manager_choose_frame(FRAME_SPLASH_SCREEN);
	// Dessine le splash avant d'allumer l'écran pour éviter le flash au boot
	lv_refr_now(NULL);
	vTaskDelay(pdMS_TO_TICKS(20)); // fin du dernier transfert DMA
	LCD_Display_On();
	Volume_adjustment(10);
	Play_Music("/sdcard", "startup.mp3");
	Driver_Init();

	const TickType_t boot_tick = xTaskGetTickCount();

	while (1) {

		int msg;
		if (xQueueReceive(app_msg_queue, &msg, 0)) {
			if (msg == MSG_TIME_READY) {
				printf("Heure OK → changement de frame\n");
				app_start();
				// Le WiFi ne sert qu'à la synchro de l'heure au démarrage
				if (app_manager_time_is_synced()) {
					WIFI_Stop();
				}
			} else {
				app_manager_handle_msg(msg);
			}
		}

		// Pas d'heure au bout du délai : on démarre quand même, sans bloquer
		// l'enfant sur le splash. Le Wi-Fi continue d'essayer en fond.
		if (!app_started && xTaskGetTickCount() - boot_tick >
								pdMS_TO_TICKS(TIME_SYNC_TIMEOUT_MS)) {
			printf("Heure non synchronisée après %d s, démarrage quand même\n",
				   TIME_SYNC_TIMEOUT_MS / 1000);
			app_start();
		}

		wakeup();

		vTaskDelay(pdMS_TO_TICKS(10));
		lv_timer_handler();
		LVGL_Screen_Sleep_Loop();
	}
}
