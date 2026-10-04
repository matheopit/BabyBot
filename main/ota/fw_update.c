#include "fw_update.h"
#include "ST7789.h"
#include "Wireless.h"
#include "app_manager.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "lvgl.h"
#include "psa/crypto.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static const char *TAG = "FW_UPDATE";

#define FW_UPDATE_TMP FW_UPDATE_DIR "/firmware.tmp"
#define FW_UPDATE_BAD FW_UPDATE_DIR "/firmware.bad" // refusé par le flash
#define RELEASE_API_URL                                                        \
	"https://api.github.com/repos/" FW_UPDATE_REPO "/releases/latest"
#define RELEASE_JSON_MAX (64 * 1024)
#define CHUNK_SIZE 4096

// ===============================
// VERSIONS
// ===============================
// "v1.2.3", "1.2.3" ou "1.2.3-4-gabcdef" -> {1, 2, 3}
static bool parse_version(const char *s, int v[3]) {
	if (*s == 'v' || *s == 'V')
		s++;
	return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) == 3;
}

// <0 si a < b, 0 si égales, >0 si a > b
static int compare_versions(const int a[3], const int b[3]) {
	for (int i = 0; i < 3; i++) {
		if (a[i] != b[i])
			return a[i] - b[i];
	}
	return 0;
}

// Lit l'en-tête esp_app_desc_t d'un .bin et vérifie qu'il s'agit bien d'un
// firmware BabyBot
static bool read_image_desc(const char *path, esp_app_desc_t *desc) {
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	bool ok = fseek(f,
					sizeof(esp_image_header_t) +
						sizeof(esp_image_segment_header_t),
					SEEK_SET) == 0 &&
			  fread(desc, 1, sizeof(*desc), f) == sizeof(*desc);
	fclose(f);
	if (!ok || desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
		ESP_LOGE(TAG, "%s n'est pas un firmware ESP32", path);
		return false;
	}
	if (strncmp(desc->project_name, esp_app_get_description()->project_name,
				sizeof(desc->project_name)) != 0) {
		ESP_LOGE(TAG, "%s est le firmware de « %.32s », pas de « %.32s »",
				 path, desc->project_name,
				 esp_app_get_description()->project_name);
		return false;
	}
	return true;
}

// ===============================
// FLASH DEPUIS LA SD (au démarrage)
// ===============================
// Avancement lu par l'écran de mise à jour (tâche LVGL)
static volatile int flash_percent = 0;
static const char *volatile flash_error = NULL;

bool fw_update_pending(void) {
	struct stat st;
	return stat(FW_UPDATE_FILE, &st) == 0;
}

static const char *flash_from_sd(void) {
	esp_app_desc_t desc;
	if (!read_image_desc(FW_UPDATE_FILE, &desc))
		return "Fichier invalide";
	ESP_LOGI(TAG, "Flash de %s : version %.32s (actuelle %.32s)",
			 FW_UPDATE_FILE, desc.version, esp_app_get_description()->version);

	const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
	if (!part) {
		ESP_LOGE(TAG, "Pas de partition OTA : reflasher partitions.csv en USB");
		return "Pas de partition OTA";
	}

	FILE *f = fopen(FW_UPDATE_FILE, "rb");
	if (!f)
		return "Lecture SD impossible";
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > part->size) {
		fclose(f);
		ESP_LOGE(TAG, "Taille %ld incompatible avec %s (%lu octets)", size,
				 part->label, (unsigned long)part->size);
		return "Firmware trop gros";
	}

	esp_ota_handle_t ota;
	esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
	if (err != ESP_OK) {
		fclose(f);
		ESP_LOGE(TAG, "esp_ota_begin : %s", esp_err_to_name(err));
		return "Erreur flash";
	}

	char *buf = malloc(CHUNK_SIZE);
	long done = 0;
	while (buf && done < size) {
		size_t n = fread(buf, 1, CHUNK_SIZE, f);
		if (n == 0)
			break;
		err = esp_ota_write(ota, buf, n);
		if (err != ESP_OK)
			break;
		done += n;
		flash_percent = done * 100 / size;
	}
	free(buf);
	fclose(f);

	if (done != size || err != ESP_OK) {
		esp_ota_abort(ota);
		ESP_LOGE(TAG, "Écriture interrompue à %ld/%ld octets (%s)", done, size,
				 esp_err_to_name(err));
		return "Erreur d'ecriture";
	}
	// esp_ota_end vérifie l'image complète (segments, SHA-256)
	err = esp_ota_end(ota);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Image refusée : %s", esp_err_to_name(err));
		return "Image corrompue";
	}
	err = esp_ota_set_boot_partition(part);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition : %s", esp_err_to_name(err));
		return "Erreur flash";
	}
	ESP_LOGI(TAG, "Firmware %.32s écrit dans %s", desc.version, part->label);
	return NULL;
}

static void flash_task(void *arg) {
	const char *error = flash_from_sd();
	if (error) {
		// On le met de côté pour ne pas reboucler à chaque démarrage
		unlink(FW_UPDATE_BAD);
		rename(FW_UPDATE_FILE, FW_UPDATE_BAD);
		flash_error = error;
		vTaskDelay(pdMS_TO_TICKS(5000));
	} else {
		unlink(FW_UPDATE_FILE);
		flash_percent = 100;
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	esp_restart();
}

static lv_obj_t *update_bar;
static lv_obj_t *update_label;

static void update_screen_timer_cb(lv_timer_t *t) {
	if (flash_error) {
		lv_label_set_text_fmt(update_label, "Echec : %s", flash_error);
		lv_obj_set_style_text_color(update_label, lv_color_hex(0xFF5050), 0);
		return;
	}
	lv_bar_set_value(update_bar, flash_percent, LV_ANIM_OFF);
	lv_label_set_text_fmt(update_label, "%d %%", flash_percent);
}

void fw_update_apply_from_sd(void) {
	lv_obj_t *scr = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	// Pas d'accents : les polices Montserrat intégrées sont en ASCII
	lv_obj_t *title = lv_label_create(scr);
	lv_label_set_text(title, "Mise a jour\ndu firmware");
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(COLOR_MAIN), 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

	update_bar = lv_bar_create(scr);
	lv_obj_set_size(update_bar, 180, 14);
	lv_obj_set_style_bg_color(update_bar, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_border_color(update_bar, lv_color_hex(COLOR_MAIN),
								  LV_PART_MAIN);
	lv_obj_set_style_border_width(update_bar, 1, LV_PART_MAIN);
	lv_obj_set_style_pad_all(update_bar, 3, LV_PART_MAIN);
	lv_obj_set_style_bg_color(update_bar, lv_color_hex(COLOR_MAIN),
							  LV_PART_INDICATOR);
	lv_bar_set_range(update_bar, 0, 100);
	lv_obj_align(update_bar, LV_ALIGN_CENTER, 0, 0);

	update_label = lv_label_create(scr);
	lv_obj_set_style_text_color(update_label, lv_color_hex(COLOR_MAIN), 0);
	lv_label_set_text(update_label, "0 %");
	lv_obj_align(update_label, LV_ALIGN_CENTER, 0, 30);

	lv_scr_load(scr);
	lv_timer_create(update_screen_timer_cb, 200, NULL);
	lv_refr_now(NULL);
	vTaskDelay(pdMS_TO_TICKS(20)); // fin du dernier transfert DMA
	LCD_Display_On();

	xTaskCreatePinnedToCore(flash_task, "FW flash", 4096, NULL, 3, NULL, 0);
	// Seule boucle LVGL jusqu'au redémarrage (pas de Wi-Fi, NFC ni réveil)
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(10));
		lv_timer_handler();
	}
}

// ===============================
// TÉLÉCHARGEMENT DEPUIS GITHUB
// ===============================
// GET avec suivi des redirections (les assets GitHub redirigent vers un
// autre domaine). Retourne le client prêt à lire le corps, ou NULL.
static esp_http_client_handle_t http_open(const char *url, const char *accept) {
	esp_http_client_config_t cfg = {
		.url = url,
		.crt_bundle_attach = esp_crt_bundle_attach,
		.user_agent = "BabyBot", // exigé par l'API GitHub
		.timeout_ms = 15000,
		.buffer_size = 4096,
		.buffer_size_tx = 2048, // URL de redirection signée très longue
	};
	esp_http_client_handle_t client = esp_http_client_init(&cfg);
	if (!client)
		return NULL;
	if (accept)
		esp_http_client_set_header(client, "Accept", accept);

	for (int i = 0; i < 5; i++) {
		esp_err_t err = esp_http_client_open(client, 0);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Connexion impossible : %s", esp_err_to_name(err));
			break;
		}
		if (esp_http_client_fetch_headers(client) < 0) {
			ESP_LOGE(TAG, "Pas de réponse HTTP");
			break;
		}
		int status = esp_http_client_get_status_code(client);
		if (status == 200)
			return client;
		if (status != 301 && status != 302 && status != 303 && status != 307 &&
			status != 308) {
			ESP_LOGE(TAG, "HTTP %d", status);
			break;
		}
		esp_http_client_flush_response(client, NULL);
		if (esp_http_client_set_redirection(client) != ESP_OK)
			break;
	}
	esp_http_client_cleanup(client);
	return NULL;
}

static void http_close(esp_http_client_handle_t client) {
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
}

// Corps de la réponse en chaîne terminée par '\0' (à libérer), ou NULL
static char *http_get_text(const char *url, const char *accept) {
	esp_http_client_handle_t client = http_open(url, accept);
	if (!client)
		return NULL;
	char *buf = malloc(RELEASE_JSON_MAX);
	int len = 0;
	while (buf && len < RELEASE_JSON_MAX - 1) {
		int n = esp_http_client_read(client, buf + len,
									 RELEASE_JSON_MAX - 1 - len);
		if (n < 0) {
			ESP_LOGE(TAG, "Lecture HTTP interrompue");
			free(buf);
			buf = NULL;
			break;
		}
		if (n == 0)
			break;
		len += n;
	}
	http_close(client);
	if (buf)
		buf[len] = '\0';
	return buf;
}

// Télécharge url dans path ; vérifie la taille et, si GitHub la fournit
// ("sha256:<hex>"), l'empreinte SHA-256
static bool download_to_file(const char *url, const char *path, int size,
							 const char *digest) {
	esp_http_client_handle_t client = http_open(url, NULL);
	if (!client)
		return false;
	FILE *f = fopen(path, "wb");
	char *buf = malloc(CHUNK_SIZE);
	if (!f || !buf) {
		ESP_LOGE(TAG, "Impossible d'écrire %s", path);
		if (f)
			fclose(f);
		free(buf);
		http_close(client);
		return false;
	}

	psa_crypto_init();
	psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
	psa_hash_setup(&sha, PSA_ALG_SHA_256);
	int done = 0;
	bool ok = true;
	while (ok) {
		int n = esp_http_client_read(client, buf, CHUNK_SIZE);
		if (n == 0)
			break;
		ok = n > 0 && fwrite(buf, 1, n, f) == (size_t)n;
		if (ok) {
			psa_hash_update(&sha, (const uint8_t *)buf, n);
			done += n;
			if (done / (100 * 1024) != (done - n) / (100 * 1024))
				ESP_LOGI(TAG, "%d / %d Ko", done / 1024, size / 1024);
		}
	}
	uint8_t hash[32];
	size_t hash_len = 0;
	if (psa_hash_finish(&sha, hash, sizeof(hash), &hash_len) != PSA_SUCCESS)
		ok = false;
	free(buf);
	ok = (fclose(f) == 0) && ok;
	http_close(client);

	if (!ok || done != size) {
		ESP_LOGE(TAG, "Téléchargement incomplet : %d/%d octets", done, size);
		return false;
	}
	if (digest && strncmp(digest, "sha256:", 7) == 0) {
		char hex[65];
		for (int i = 0; i < 32; i++)
			sprintf(hex + 2 * i, "%02x", hash[i]);
		if (strcasecmp(hex, digest + 7) != 0) {
			ESP_LOGE(TAG, "SHA-256 différent : %s au lieu de %s", hex,
					 digest + 7);
			return false;
		}
	}
	return true;
}

// true si une version plus récente a été téléchargée dans FW_UPDATE_FILE
static bool check_github(void) {
	int current[3];
	const char *current_str = esp_app_get_description()->version;
	if (!parse_version(current_str, current)) {
		ESP_LOGW(TAG, "Version courante « %s » non numérotée (version.txt) : "
					  "pas de mise à jour",
				 current_str);
		return false;
	}

	char *json = http_get_text(RELEASE_API_URL, "application/vnd.github+json");
	if (!json)
		return false;
	cJSON *root = cJSON_Parse(json);
	free(json);
	if (!root) {
		ESP_LOGE(TAG, "Réponse GitHub invalide");
		return false;
	}

	bool downloaded = false;
	cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
	int latest[3];
	if (!cJSON_IsString(tag) || !parse_version(tag->valuestring, latest)) {
		ESP_LOGW(TAG, "Pas de release vX.Y.Z sur %s", FW_UPDATE_REPO);
		goto out;
	}
	if (compare_versions(latest, current) <= 0) {
		ESP_LOGI(TAG, "Firmware à jour (%s, dernière release %s)", current_str,
				 tag->valuestring);
		goto out;
	}

	// Cette version a déjà démarré puis été annulée par le bootloader : on
	// ne la reflashe pas à chaque démarrage
	const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
	esp_app_desc_t invalid_desc;
	int invalid_ver[3];
	if (invalid &&
		esp_ota_get_partition_description(invalid, &invalid_desc) == ESP_OK &&
		parse_version(invalid_desc.version, invalid_ver) &&
		compare_versions(invalid_ver, latest) == 0) {
		ESP_LOGW(TAG, "%s a déjà échoué au démarrage, ignorée",
				 tag->valuestring);
		goto out;
	}

	cJSON *asset = NULL;
	cJSON_ArrayForEach(asset, cJSON_GetObjectItem(root, "assets")) {
		cJSON *name = cJSON_GetObjectItem(asset, "name");
		if (cJSON_IsString(name) && strcmp(name->valuestring, FW_UPDATE_ASSET) == 0)
			break;
	}
	cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
	cJSON *size = cJSON_GetObjectItem(asset, "size");
	cJSON *digest = cJSON_GetObjectItem(asset, "digest");
	if (!cJSON_IsString(url) || !cJSON_IsNumber(size)) {
		ESP_LOGE(TAG, "Release %s sans fichier %s", tag->valuestring,
				 FW_UPDATE_ASSET);
		goto out;
	}

	ESP_LOGI(TAG, "Nouvelle version %s (actuelle %s), téléchargement de %d Ko",
			 tag->valuestring, current_str, size->valueint / 1024);
	struct stat st;
	if (stat(FW_UPDATE_DIR, &st) != 0 && mkdir(FW_UPDATE_DIR, 0775) != 0) {
		ESP_LOGE(TAG, "Impossible de créer %s : %s", FW_UPDATE_DIR,
				 strerror(errno));
		goto out;
	}
	esp_app_desc_t desc;
	if (download_to_file(url->valuestring, FW_UPDATE_TMP, size->valueint,
						 cJSON_IsString(digest) ? digest->valuestring : NULL) &&
		read_image_desc(FW_UPDATE_TMP, &desc)) {
		unlink(FW_UPDATE_FILE);
		downloaded = rename(FW_UPDATE_TMP, FW_UPDATE_FILE) == 0;
	}
	if (downloaded)
		ESP_LOGI(TAG, "%s prêt dans %s", tag->valuestring, FW_UPDATE_FILE);
	else
		unlink(FW_UPDATE_TMP);

out:
	cJSON_Delete(root);
	return downloaded;
}

static void check_task(void *arg) {
	bool ready = check_github();
	// Le Wi-Fi ne sert qu'à l'heure et à la mise à jour au démarrage
	WIFI_Stop();
	if (ready) {
		int msg = MSG_FW_READY;
		xQueueSend(app_msg_queue, &msg, pdMS_TO_TICKS(100));
	}
	vTaskDelete(NULL);
}

void fw_update_check_start(void) {
	static bool started = false;
	if (started)
		return;
	started = true;
	// TLS + cJSON : grosse pile. Cœur 1 : sur le cœur 0, la boucle LVGL
	// (app_main, priorité 1) serait préemptée pendant le handshake TLS
	xTaskCreatePinnedToCore(check_task, "FW check", 8192, NULL, 2, NULL, 1);
}

void fw_update_mark_valid(void) {
	esp_ota_img_states_t state;
	if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) ==
			ESP_OK &&
		state == ESP_OTA_IMG_PENDING_VERIFY) {
		esp_ota_mark_app_valid_cancel_rollback();
		ESP_LOGI(TAG, "Nouveau firmware %s validé",
				 esp_app_get_description()->version);
	}
}
