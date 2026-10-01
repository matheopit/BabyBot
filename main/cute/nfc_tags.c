#include "nfc_tags.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "NFC_TAGS";

#define SD_ROOT "/sdcard"

// Lit tout le fichier dans un tampon terminé par '\0' (à libérer), ou NULL
static char *read_file(const char *file_path) {
	FILE *f = fopen(file_path, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}

	char *buffer = malloc(size + 1);
	if (buffer) {
		size_t len = fread(buffer, 1, size, f);
		buffer[len] = '\0';
	}
	fclose(f);
	return buffer;
}

// Copie le chemin dans path, préfixé par SD_ROOT s'il est relatif
static bool resolve_path(const char *raw, char *path, size_t path_len) {
	int written;
	if (raw[0] == '/')
		written = snprintf(path, path_len, "%s", raw);
	else
		written = snprintf(path, path_len, "%s/%s", SD_ROOT, raw);
	if (written < 0 || (size_t)written >= path_len) {
		ESP_LOGE(TAG, "Chemin trop long : %s", raw);
		return false;
	}
	// Retire un éventuel '/' final ("/sdcard/comptines/")
	size_t len = strlen(path);
	if (len > 1 && path[len - 1] == '/')
		path[len - 1] = '\0';
	return true;
}

bool nfc_tags_lookup(const char *uid_hex, char *path, size_t path_len,
					 bool *is_dir) {
	char *json = read_file(NFC_TAGS_FILE_PATH);
	if (!json) {
		ESP_LOGW(TAG, "%s absent ou vide", NFC_TAGS_FILE_PATH);
		return false;
	}

	// Ignore le BOM UTF-8 ajouté par certains éditeurs (Bloc-notes Windows)
	const char *text = json;
	if (strncmp(text, "\xEF\xBB\xBF", 3) == 0)
		text += 3;

	cJSON *root = cJSON_Parse(text);
	if (!root) {
		// Montre le texte à l'endroit de l'erreur pour la retrouver
		const char *err = cJSON_GetErrorPtr();
		ESP_LOGE(TAG, "JSON invalide dans %s, près de : %.40s",
				 NFC_TAGS_FILE_PATH, err ? err : "?");
		free(json);
		return false;
	}
	free(json);

	bool found = false;
	cJSON *tags = cJSON_GetObjectItem(root, "tags");
	cJSON *entry;
	cJSON_ArrayForEach(entry, tags) {
		cJSON *uid = cJSON_GetObjectItem(entry, "uid");
		cJSON *raw_path = cJSON_GetObjectItem(entry, "path");
		if (!cJSON_IsString(uid) || !cJSON_IsString(raw_path))
			continue;
		if (strcasecmp(uid->valuestring, uid_hex) != 0)
			continue;

		found = resolve_path(raw_path->valuestring, path, path_len);
		break;
	}
	cJSON_Delete(root);

	if (!found)
		return false;

	struct stat st;
	if (stat(path, &st) != 0) {
		ESP_LOGE(TAG, "Tag %s : %s introuvable", uid_hex, path);
		return false;
	}
	*is_dir = S_ISDIR(st.st_mode);
	return true;
}
