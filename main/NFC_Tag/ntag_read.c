#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_manager.h"
#include "pn532.h"
#include "pn532_driver_hsu.h"
#include "pn532_driver_i2c.h"
#include "pn532_driver_spi.h"
#include "sdkconfig.h"

// select ONLY ONE interface for the PN532
#define PN532_MODE_I2C 0
#define PN532_MODE_HSU 1
#define PN532_MODE_SPI 0

#if PN532_MODE_I2C

// I2C mode needs only SDA, SCL and IRQ pins. RESET pin will be used if valid.
// IRQ pin can be used in polling mode or in interrupt mode. Use menuconfig to
// select mode.
#define SCL_PIN (10)
#define SDA_PIN (11)
#define RESET_PIN (-1)
#define IRQ_PIN (-1)

#elif PN532_MODE_HSU

// HSU mode needs only RX/TX pins. RESET pin will be used if valid.
#define RESET_PIN (-1)
#define IRQ_PIN (-1)
#define HSU_HOST_RX (43)
#define HSU_HOST_TX (44)
#define HSU_UART_PORT UART_NUM_0
#define HSU_BAUD_RATE (921600)

#elif PN532_MODE_SPI

// SPI mode needs only CS, SCK, MISO and MOSI pins.
// If at least one of CS, SCK, MISO and MOSI is set to -1 the SPI host must be
// configured outside the library. IRQ pin can be used in polling mode or in
// interrupt mode. Use menuconfig to select mode.
#define RESET_PIN (-1)
#define IRQ_PIN (6)
// #define IRQ_PIN        (-1)
#define SPI_CS (5)
#define SPI_SCK (2)
#define SPI_MISO (3)
#define SPI_MOSI (4)
#define SPI_HOST_NFC (SPI3_HOST)
#define SPI_CLOCKRATE (1000000)

#endif

#define NFC_POLL_PERIOD_MS 1000 // intervalle entre deux recherches de tag
#define NFC_CMD_TIMEOUT_MS 200
// Nombre d'essais d'activation par recherche (0xFF = attente infinie)
#define NFC_PASSIVE_RETRIES 0x04
// Lectures ratées consécutives avant de considérer le tag comme retiré
#define NFC_MISSES_BEFORE_REMOVED 2
// PN532 mis en veille entre deux recherches (0 : toujours éveillé)
#define NFC_POWER_DOWN 1
// Temps laissé à l'oscillateur du PN532 pour redémarrer après le réveil
#define NFC_WAKEUP_DELAY_MS 10

static const char *TAG = "ntag_read";
static pn532_io_t pn532_io;

void init_nfc() {
	esp_err_t err;
	pn532_release(&pn532_io);
	printf("init_nfc\n");

#if 0
    // Enable DEBUG logging
    esp_log_level_set("PN532", ESP_LOG_DEBUG);
    esp_log_level_set("pn532_driver", ESP_LOG_DEBUG);
    esp_log_level_set("pn532_driver_i2c", ESP_LOG_DEBUG);
    esp_log_level_set("i2c.master", ESP_LOG_DEBUG);
    esp_log_level_set("pn532_driver_hsu", ESP_LOG_DEBUG);
    esp_log_level_set("pn532_driver_spi", ESP_LOG_DEBUG);
    esp_log_level_set("spi", ESP_LOG_DEBUG);
#endif

	vTaskDelay(1000 / portTICK_PERIOD_MS);

#if PN532_MODE_I2C

	ESP_LOGI(TAG, "init PN532 in I2C mode");
	ESP_ERROR_CHECK(pn532_new_driver_i2c(SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN,
										 0, &pn532_io));

#elif PN532_MODE_HSU

	ESP_LOGI(TAG, "init PN532 in HSU mode");
	ESP_ERROR_CHECK(pn532_new_driver_hsu(HSU_HOST_RX, HSU_HOST_TX, RESET_PIN,
										 IRQ_PIN, HSU_UART_PORT, HSU_BAUD_RATE,
										 &pn532_io));

#elif PN532_MODE_SPI

	ESP_LOGI(TAG, "init PN532 in SPI mode");
	ESP_ERROR_CHECK(pn532_new_driver_spi(SPI_MISO, SPI_MOSI, SPI_SCK, SPI_CS,
										 -1, IRQ_PIN, SPI_HOST_NFC,
										 SPI_CLOCKRATE, &pn532_io));

#endif

	do {
		err = pn532_init(&pn532_io);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "failed to initialize PN532: %d", err);
			pn532_release(&pn532_io);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
	} while (err != ESP_OK);

	ESP_LOGI(TAG, "get firmware version");
	uint32_t version_data = 0;
	do {
		err = pn532_get_firmware_version(&pn532_io, &version_data);
		if (ESP_OK != err) {
			ESP_LOGI(TAG, "Didn't find PN53x board");
			pn532_reset(&pn532_io);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
	} while (ESP_OK != err);

	// Log firmware infos
	ESP_LOGI(TAG, "Found chip PN5%x",
			 (unsigned int)(version_data >> 24) & 0xFF);
	ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF,
			 (int)(version_data >> 8) & 0xFF);

	// Une recherche se termine rapidement même sans tag, pour couper le champ
	// RF
	err = pn532_set_passive_activation_retries(&pn532_io, NFC_PASSIVE_RETRIES);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "failed to set passive activation retries: %d", err);
	}

	ESP_LOGI(TAG, "Waiting for an ISO14443A Card ...");
}

#if NFC_POWER_DOWN
// Met le PN532 en veille (champ RF coupé, quelques dizaines de µA).
// Il se réveille sur l'UART : voir nfc_wake_up().
static esp_err_t nfc_power_down(void) {
	uint8_t cmd[] = {PN532_COMMAND_POWERDOWN, 0x10}; // 0x10 : réveil par HSU
	esp_err_t err = pn532_send_command_wait_ack(&pn532_io, cmd, sizeof(cmd),
												NFC_CMD_TIMEOUT_MS);
	if (err == ESP_OK) {
		uint8_t resp[16];
		err =
			pn532_read_data(&pn532_io, resp, sizeof(resp), NFC_CMD_TIMEOUT_MS);
	}
	return err;
}

// Réveille le PN532. Le préambule de réveil de la lib enchaîne la commande
// sans attendre : à 921600 bauds le PN532 n'a pas le temps de redémarrer et
// la commande est perdue. On envoie donc le réveil nous-mêmes, puis on attend.
static esp_err_t nfc_wake_up(void) {
	static const uint8_t wakeup_frame[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00,
										   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
										   0x00, 0x00, 0x00, 0x00};
	uart_write_bytes(HSU_UART_PORT, wakeup_frame, sizeof(wakeup_frame));
	uart_wait_tx_done(HSU_UART_PORT, pdMS_TO_TICKS(100));
	vTaskDelay(pdMS_TO_TICKS(NFC_WAKEUP_DELAY_MS));
	uart_flush_input(HSU_UART_PORT);

	// Réveil déjà fait : la lib ne doit pas renvoyer son préambule
	pn532_io.isSAMConfigDone = true;
	// Reconfigure le SAM (mode normal) et vérifie que le PN532 répond
	return pn532_SAM_config(&pn532_io);
}
#endif

// Cherche un tag toutes les NFC_POLL_PERIOD_MS, PN532 en veille entre deux.
// Prévient app_manager quand un nouveau tag est posé ou quand il est retiré ;
// un tag qui reste posé ne génère pas de nouvel événement.
void nfc_loop() {
	nfc_tag_t current = {0}; // tag actuellement posé (len = 0 : aucun)
	int misses = 0;			 // lectures ratées consécutives

	while (1) {
#if NFC_POWER_DOWN
		if (nfc_wake_up() != ESP_OK) {
			ESP_LOGW(TAG, "PN532 wake up failed");
		}
#endif
		nfc_tag_t tag = {0};
		esp_err_t err = pn532_read_passive_target_id(
			&pn532_io, PN532_BRTY_ISO14443A_106KBPS, tag.uid, &tag.len,
			NFC_CMD_TIMEOUT_MS);

		if (err == ESP_OK && tag.len > 0 && tag.len <= NFC_UID_MAX_LEN) {
			misses = 0;
			if (tag.len != current.len ||
				memcmp(tag.uid, current.uid, tag.len) != 0) {
				ESP_LOGI(TAG, "Tag posé :");
				ESP_LOG_BUFFER_HEX_LEVEL(TAG, tag.uid, tag.len, ESP_LOG_INFO);
				current = tag;
				app_manager_notify(EVENT_NFC_TAG, &current);
			}
		} else if (current.len > 0 && ++misses >= NFC_MISSES_BEFORE_REMOVED) {
			current.len = 0;
			misses = 0;
			app_manager_notify(EVENT_NFC_TAG_REMOVED, NULL);
		}
#if NFC_POWER_DOWN
		if (nfc_power_down() != ESP_OK) {
			ESP_LOGW(TAG, "PN532 power down failed");
		}
#endif
		vTaskDelay(pdMS_TO_TICKS(NFC_POLL_PERIOD_MS));
	}
}
