#pragma once

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h> // For memcpy

extern uint16_t BLE_NUM;
extern uint16_t WIFI_NUM;
extern bool Scan_finish;

void Wireless_Init(void);
void WIFI_Init(void *arg);
void WIFI_Stop(void);
uint16_t WIFI_Scan(void);
void BLE_Init(void *arg);
uint16_t BLE_Scan(void);
bool read_wifi_json(const char *directory, const char *fileName,
					wifi_sta_config_t *out);