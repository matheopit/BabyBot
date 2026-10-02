#include "BAT_Driver.h"

const static char *ADC_TAG = "ADC";

volatile float BAT_analogVolts = 0;

// Une ligne de log de la tension toutes les BAT_LOG_PERIOD mesures (1 par s)
#define BAT_LOG_PERIOD 60

static bat_filter_t bat_filter;
static volatile bat_state_t bat_state = BAT_STATE_OK;
static volatile bool bat_valid = false;

/*---------------------------------------------------------------
		ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
										 adc_atten_t atten,
										 adc_cali_handle_t *out_handle) {
	adc_cali_handle_t handle = NULL;
	esp_err_t ret = ESP_FAIL;
	bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
	if (!calibrated) {
		ESP_LOGI(ADC_TAG, "calibration scheme version is %s", "Curve Fitting");
		adc_cali_curve_fitting_config_t cali_config = {
			.unit_id = unit,
			.chan = channel,
			.atten = atten,
			.bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
		if (ret == ESP_OK) {
			calibrated = true;
		}
	}
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
	if (!calibrated) {
		ESP_LOGI(ADC_TAG, "calibration scheme version is %s", "Line Fitting");
		adc_cali_line_fitting_config_t cali_config = {
			.unit_id = unit,
			.atten = atten,
			.bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
		if (ret == ESP_OK) {
			calibrated = true;
		}
	}
#endif

	*out_handle = handle;
	if (ret == ESP_OK) {
		ESP_LOGI(ADC_TAG, "Calibration Success");
	} else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
		ESP_LOGW(ADC_TAG, "eFuse not burnt, skip software calibration");
	} else {
		ESP_LOGE(ADC_TAG, "Invalid arg or no memory");
	}

	return calibrated;
}

// static void example_adc_calibration_deinit(adc_cali_handle_t handle)
// {
// #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
//     ESP_LOGI(ADC_TAG, "deregister %s calibration scheme", "Curve Fitting");
//     ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

// #elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
//     ESP_LOGI(ADC_TAG, "deregister %s calibration scheme", "Line Fitting");
//     ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
// #endif
// }

adc_oneshot_unit_handle_t adc1_handle;
bool do_calibration1_chan3;
adc_cali_handle_t adc1_cali_chan3_handle = NULL;

void ADC_Init(void) {
	//-------------ADC1 Init---------------//
	adc_oneshot_unit_init_cfg_t init_config1 = {
		.unit_id = ADC_UNIT_1,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

	//-------------ADC1 Config---------------//
	adc_oneshot_chan_cfg_t config = {
		.atten = EXAMPLE_ADC_ATTEN,
		.bitwidth = ADC_BITWIDTH_DEFAULT,
	};
	ESP_ERROR_CHECK(
		adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN, &config));

	//-------------ADC1 Calibration Init---------------//
	do_calibration1_chan3 = example_adc_calibration_init(
		ADC_UNIT_1, EXAMPLE_ADC1_CHAN, EXAMPLE_ADC_ATTEN,
		&adc1_cali_chan3_handle);

	// //Tear Down
	// ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
	// if (do_calibration1_chan3) {
	//     example_adc_calibration_deinit(adc1_cali_chan3_handle);
	// }
}

void BAT_Init(void) {
	ADC_Init();
	bat_filter_reset(&bat_filter);
}

// Moyenne BAT_OVERSAMPLE lectures ADC calibrées et remonte à la tension de
// la batterie. Renvoie false si aucune lecture n'a abouti.
static bool BAT_Read_Volts(float *volts) {
	if (!do_calibration1_chan3) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			ESP_LOGE(ADC_TAG, "ADC non calibré : tension batterie non mesurée");
		}
		return false;
	}

	int sum_mv = 0;
	int ok = 0;
	for (int i = 0; i < BAT_OVERSAMPLE; i++) {
		int raw = 0;
		int mv = 0;
		if (adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN, &raw) != ESP_OK)
			continue;
		if (adc_cali_raw_to_voltage(adc1_cali_chan3_handle, raw, &mv) != ESP_OK)
			continue;
		sum_mv += mv;
		ok++;
	}
	if (ok == 0)
		return false;

	*volts = (float)((double)sum_mv / ok * BAT_DIVIDER_RATIO / 1000.0 /
					 Measurement_offset);
	return true;
}

bool BAT_Update(void) {
	static uint32_t log_tick = 0;
	static uint8_t errors = 0;

	float volts;
	if (!BAT_Read_Volts(&volts)) {
		// Un seul message au début d'une série d'échecs, puis un par minute
		if (do_calibration1_chan3 &&
			(errors == 0 || errors % BAT_LOG_PERIOD == 0))
			ESP_LOGW(ADC_TAG, "Lecture de la batterie impossible");
		if (errors < 255)
			errors++;
		return false;
	}
	errors = 0;

	BAT_analogVolts = bat_filter_push(&bat_filter, volts);
	bat_valid = true;

	bat_state_t next = bat_state_next(bat_state, BAT_analogVolts);
	bool changed = next != bat_state;
	bat_state = next;

	if (changed || log_tick == 0) {
		ESP_LOGI(ADC_TAG, "Batterie : %.2f V (%u %%), état %s", BAT_analogVolts,
				 (unsigned)bat_percent_from_volts(BAT_analogVolts),
				 bat_state_name(bat_state));
	}
	log_tick = (log_tick + 1) % BAT_LOG_PERIOD;
	return changed;
}

float BAT_Get_Volts(void) { return bat_valid ? BAT_analogVolts : 0; }

uint8_t BAT_Get_Percent(void) {
	return bat_valid ? bat_percent_from_volts(BAT_analogVolts) : 0;
}

bat_state_t BAT_Get_State(void) { return bat_state; }

bool BAT_Is_Valid(void) { return bat_valid; }
