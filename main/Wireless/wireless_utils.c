#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include <SD_MMC.h>
#include "Wireless.h"
static const char *TAG = "JSON_WIFI";

typedef struct {
    char ssid[64];
    char password[64];
} wifi_credentials_t;

bool read_wifi_json(const char* directory, const char* fileName, wifi_sta_config_t *out)
{
    const int maxPathLength = 100; 
    char filePath[maxPathLength];
    if (strcmp(directory, "/") == 0) {                                               
        snprintf(filePath, maxPathLength, "%s%s", directory, fileName);   
    } else {                                                            
        snprintf(filePath, maxPathLength, "%s/%s", directory, fileName);
    }
    FILE *f = Open_File(filePath);
    if (!f) {
        ESP_LOGE(TAG, "Impossible d'ouvrir %s", filePath);
        return false;
    }

    // Lire tout le fichier
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "malloc impossible");
        fclose(f);
        return false;
    }

    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);

    // Parse JSON
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        ESP_LOGE(TAG, "JSON invalide");
        return false;
    }

    // Récupérer ssid
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");

    if (!ssid || !password || !cJSON_IsString(ssid) || !cJSON_IsString(password)) {
        ESP_LOGE(TAG, "Champs manquants dans le JSON");
        cJSON_Delete(root);
        return false;
    }

    // Copier dans la structure (sans snprintf)
    strncpy((char*)out->ssid, ssid->valuestring, sizeof(out->ssid) - 1);
    out->ssid[sizeof(out->ssid) - 1] = '\0';

    strncpy((char*)out->password, password->valuestring, sizeof(out->password) - 1);
    out->password[sizeof(out->password) - 1] = '\0';

    ESP_LOGI(TAG, "SSID lu: %s", out->ssid);
    ESP_LOGI(TAG, "Password lu: %s", out->password);

    cJSON_Delete(root);
    return true;
}