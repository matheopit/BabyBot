#include "BAT_Logic.h"
#include <stddef.h>

void bat_filter_reset(bat_filter_t *filter) {
	filter->count = 0;
	filter->next = 0;
}

float bat_filter_push(bat_filter_t *filter, float volts) {
	filter->samples[filter->next] = volts;
	filter->next = (filter->next + 1) % BAT_FILTER_SAMPLES;
	if (filter->count < BAT_FILTER_SAMPLES)
		filter->count++;

	float sum = 0;
	for (uint8_t i = 0; i < filter->count; i++)
		sum += filter->samples[i];
	return sum / filter->count;
}

// Courbe LiPo 1S typique à vide (tension, pourcentage), de la plus haute à la
// plus basse
static const struct {
	float volts;
	uint8_t percent;
} lipo_curve[] = {
	{4.20f, 100}, {4.06f, 90}, {3.98f, 80}, {3.92f, 70},
	{3.87f, 60},  {3.82f, 50}, {3.79f, 40}, {3.77f, 30},
	{3.74f, 20},  {3.68f, 10}, {3.45f, 5},	{3.30f, 0},
};
#define LIPO_CURVE_LEN (sizeof(lipo_curve) / sizeof(lipo_curve[0]))

uint8_t bat_percent_from_volts(float volts) {
	if (volts >= lipo_curve[0].volts)
		return 100;
	if (volts <= lipo_curve[LIPO_CURVE_LEN - 1].volts)
		return 0;

	for (size_t i = 1; i < LIPO_CURVE_LEN; i++) {
		if (volts >= lipo_curve[i].volts) {
			float v_hi = lipo_curve[i - 1].volts;
			float v_lo = lipo_curve[i].volts;
			float p_hi = lipo_curve[i - 1].percent;
			float p_lo = lipo_curve[i].percent;
			float percent =
				p_lo + (volts - v_lo) * (p_hi - p_lo) / (v_hi - v_lo);
			return (uint8_t)(percent + 0.5f);
		}
	}
	return 0;
}

bat_state_t bat_state_next(bat_state_t current, float volts) {
	switch (current) {
	case BAT_STATE_OK:
		if (volts < BAT_CRITICAL_VOLTS)
			return BAT_STATE_CRITICAL;
		if (volts < BAT_LOW_VOLTS)
			return BAT_STATE_LOW;
		return BAT_STATE_OK;

	case BAT_STATE_LOW:
		if (volts < BAT_CRITICAL_VOLTS)
			return BAT_STATE_CRITICAL;
		if (volts > BAT_LOW_VOLTS + BAT_HYSTERESIS_VOLTS)
			return BAT_STATE_OK;
		return BAT_STATE_LOW;

	case BAT_STATE_CRITICAL:
		if (volts > BAT_LOW_VOLTS + BAT_HYSTERESIS_VOLTS)
			return BAT_STATE_OK;
		if (volts > BAT_CRITICAL_VOLTS + BAT_HYSTERESIS_VOLTS)
			return BAT_STATE_LOW;
		return BAT_STATE_CRITICAL;
	}
	return current;
}

const char *bat_state_name(bat_state_t state) {
	switch (state) {
	case BAT_STATE_OK:
		return "OK";
	case BAT_STATE_LOW:
		return "faible";
	case BAT_STATE_CRITICAL:
		return "critique";
	}
	return "?";
}
