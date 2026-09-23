#include "BAT_Driver.h"
#include "PCM5101.h"
#include "PWR_Key.h"
#include "SD_MMC.h"
#include "ST7789.h"
#include "Wireless.h"
#include "app_manager.h"
#include "ntag_read.h"
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

void app_main(void) {
	app_msg_queue = xQueueCreate(4, sizeof(int));
	SD_Init();
	LCD_Init();
	Audio_Init();
	LVGL_Init(); // returns the screen object

	/********************* Demo *********************/
	app_manager_init();
	app_manager_choose_frame(FRAME_SPLASH_SCREEN);
	Volume_adjustment(10);
	Play_Music("/sdcard", "startup.mp3");
	Driver_Init();

	while (1) {

		int msg;
		if (xQueueReceive(app_msg_queue, &msg, 0)) {
			if (msg == MSG_TIME_READY) {
				printf("Heure OK → changement de frame\n");
				app_manager_choose_frame(FRAME_SMILE);
				xTaskCreatePinnedToCore(nfc_task, "Other Driver task", 4096,
										NULL, 4, NULL, 1);
			}
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		lv_timer_handler();
	}
}
