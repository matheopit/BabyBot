#pragma once

#include "BAT_Logic.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>

/*---------------------------------------------------------------
		ADC General Macros
---------------------------------------------------------------*/
// ADC1 Channels
#define EXAMPLE_ADC1_CHAN ADC_CHANNEL_7	  // GPIO8
#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_12 // ADC_ATTEN_DB_12

// Pont diviseur de la carte entre la batterie et la broche ADC (Vbat / 3) et
// correction de gain empirique (code d'exemple Waveshare)
#define BAT_DIVIDER_RATIO 3.0
#define Measurement_offset 0.994500

// Mesures ADC moyennées à chaque lecture (réduit le bruit)
#define BAT_OVERSAMPLE 16

extern volatile float BAT_analogVolts; // tension filtrée, en volts

void BAT_Init(void);

// Lit la batterie, met à jour la tension filtrée, le pourcentage et l'état.
// À appeler une fois par seconde (le filtre moyenne BAT_FILTER_SAMPLES
// appels). Renvoie true si l'état a changé (voir BAT_Get_State).
bool BAT_Update(void);

// Tension filtrée (V). 0 tant qu'aucune mesure valide n'a abouti.
float BAT_Get_Volts(void);
// Pourcentage de charge, 0-100 (approximatif, voir bat_percent_from_volts)
uint8_t BAT_Get_Percent(void);
bat_state_t BAT_Get_State(void);
// false tant qu'aucune mesure valide n'a abouti (ADC non calibré, erreur de
// lecture...) : la tension et l'état ne sont alors pas significatifs
bool BAT_Is_Valid(void);
