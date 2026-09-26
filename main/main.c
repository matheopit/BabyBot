#include "BAT_Driver.h"
#include "PCM5101.h"
#include "PWR_Key.h"
#include "SD_MMC.h"
#include "ST7789.h"
#include "Wireless.h"
#include "app_manager.h"
#include "esp_pm.h"
#include "ntag_read.h"
#include <time.h>
QueueHandle_t app_msg_queue;

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

void Driver_Init(void) {
	PWR_Init();
	BAT_Init();
	Flash_Searching();
	xTaskCreatePinnedToCore(Driver_Loop, "Other Driver task", 4096, NULL, 3,
							NULL, 0);
}

// Gestion d'énergie : fréquence CPU dynamique et light sleep automatique
// quand plus aucune tâche n'a de travail. Le minimum reste à 80 MHz pour que
// l'APB (donc le PWM du rétroéclairage) ne change pas de fréquence.
static void PM_Init(void) {
	esp_pm_config_t pm_config = {
		.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
		.min_freq_mhz = 80,
		.light_sleep_enable = true,
	};
	esp_err_t ret = esp_pm_configure(&pm_config);
	if (ret != ESP_OK)
		printf("esp_pm_configure: %s\n", esp_err_to_name(ret));
}

void app_main(void) {
	PM_Init();
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

	while (1) {

		int msg;
		if (xQueueReceive(app_msg_queue, &msg, 0)) {
			if (msg == MSG_TIME_READY) {
				static bool time_ready_done = false;
				if (!time_ready_done) {
					time_ready_done = true;
					printf("Heure OK → changement de frame\n");
					app_manager_choose_frame(FRAME_SMILE);
					
					xTaskCreatePinnedToCore(nfc_task, "Other Driver task", 4096,
											NULL, 4, NULL, 1);
				}
				// Le WiFi ne sert qu'à la synchro de l'heure au démarrage
				if (app_manager_time_is_synced()) {
					WIFI_Stop();
				}
			}
		}

		wakeup();

		// Écran éteint : boucle plus lente pour laisser le CPU en light sleep
		// (le toucher reste lu, il suffit à rallumer l'écran)
		vTaskDelay(pdMS_TO_TICKS(LVGL_Screen_Is_Asleep() ? 100 : 10));
		LVGL_Tick_Update();
		lv_timer_handler();
		LVGL_Screen_Sleep_Loop();
	}
}
