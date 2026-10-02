#pragma once

#include <stdint.h>

// Logique batterie pure (aucune dépendance matérielle) : filtre, pourcentage
// et état avec hystérésis. Séparée de BAT_Driver.c pour pouvoir être testée
// sur PC.
//
// Batterie visée : LiPo 1S 3,7 V (4,2 V pleine).

// Seuils d'état, en volts (tension filtrée sur BAT_FILTER_SAMPLES secondes)
#define BAT_LOW_VOLTS 3.60f		 // en dessous : batterie faible
#define BAT_CRITICAL_VOLTS 3.40f // en dessous : batterie critique
// Pour remonter d'un état, la tension doit dépasser le seuil de cette marge
// (évite de basculer à chaque variation sous charge)
#define BAT_HYSTERESIS_VOLTS 0.10f

// Nombre de mesures (une par seconde) moyennées pour lisser la tension
#define BAT_FILTER_SAMPLES 10

typedef enum {
	BAT_STATE_OK = 0,
	BAT_STATE_LOW,
	BAT_STATE_CRITICAL,
} bat_state_t;

// Moyenne glissante sur les BAT_FILTER_SAMPLES dernières mesures
typedef struct {
	float samples[BAT_FILTER_SAMPLES];
	uint8_t count; // mesures valides (<= BAT_FILTER_SAMPLES)
	uint8_t next;  // prochaine case à écrire
} bat_filter_t;

void bat_filter_reset(bat_filter_t *filter);
// Ajoute une mesure et renvoie la moyenne des mesures disponibles (donc
// valable dès la première mesure)
float bat_filter_push(bat_filter_t *filter, float volts);

// Pourcentage de charge (0-100) à partir de la tension, d'après une courbe
// LiPo 1S typique à vide, interpolée. Approximatif : sous charge la tension
// baisse, le pourcentage est alors un peu sous-estimé.
uint8_t bat_percent_from_volts(float volts);

// État suivant à partir de l'état courant et de la tension filtrée, avec
// hystérésis. Peut passer directement de OK à CRITICAL.
bat_state_t bat_state_next(bat_state_t current, float volts);

const char *bat_state_name(bat_state_t state);
