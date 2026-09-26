#include "PCM5101.h"

static const char *TAG = "AUDIO PCM5101";

static i2s_chan_handle_t i2s_tx_chan;
static i2s_chan_handle_t i2s_rx_chan;

uint8_t Volume = Volume_MAX - 2;


// Suivi de la position de lecture : format I2S courant (mis a jour par
// bsp_i2s_reconfig_clk) et nombre de trames audio envoyees depuis le debut
// du morceau. Ecrit par la tache du lecteur, lu par la tache LVGL.
static uint32_t s_sample_rate = 44100;
static uint32_t s_bytes_per_frame = 4; // 16 bits x 2 canaux
static volatile uint32_t s_frames_played = 0;
static volatile bool s_track_finished = false;
static uint32_t s_duration_sec = 0;
// static esp_err_t bsp_i2s_write(void *audio_buffer, size_t len, size_t
// *bytes_written, uint32_t timeout_ms) {                     // I2S Write Init
//     return i2s_channel_write(i2s_tx_chan, (char *)audio_buffer, len,
//     bytes_written, timeout_ms);
// }
static esp_err_t bsp_i2s_write(void *audio_buffer, size_t len,
							   size_t *bytes_written, uint32_t timeout_ms) {
	int16_t *samples = (int16_t *)audio_buffer;
	size_t sample_count = len / sizeof(int16_t);

	// Calculate the volume scaling factor to convert the volume level from
	// 0-100 to the 0.0-1.0 range
	float volume_factor = Volume / 100.0f;

	for (size_t i = 0; i < sample_count; i++) {
		samples[i] = (int16_t)(samples[i] * volume_factor);
	}

	esp_err_t ret = i2s_channel_write(i2s_tx_chan, (char *)audio_buffer, len,
									  bytes_written, timeout_ms);
	if (s_bytes_per_frame)
		s_frames_played += *bytes_written / s_bytes_per_frame;
	return ret;
}
static esp_err_t bsp_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg,
									  i2s_slot_mode_t ch) { // I2S Init
	esp_err_t ret = ESP_OK;
	i2s_std_config_t std_cfg = {
		.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
		.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
			(i2s_data_bit_width_t)bits_cfg, ch),
		.gpio_cfg = BSP_I2S_GPIO_CFG,
	};
	s_sample_rate = rate;
	s_bytes_per_frame = (bits_cfg / 8) * (ch == I2S_SLOT_MODE_MONO ? 1 : 2);
	ret |= i2s_channel_disable(i2s_tx_chan);
	ret |= i2s_channel_reconfig_std_clock(i2s_tx_chan, &std_cfg.clk_cfg);
	ret |= i2s_channel_reconfig_std_slot(i2s_tx_chan, &std_cfg.slot_cfg);
	ret |= i2s_channel_enable(i2s_tx_chan);
	return ret;
}

static esp_err_t
audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting) { // audio mute function
	ESP_LOGI(TAG, "mute setting %d", setting);
	return ESP_OK;
}

static esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config,
								i2s_chan_handle_t *tx_channel,
								i2s_chan_handle_t *rx_channel) { // Audio Init
	i2s_chan_config_t chan_cfg =
		I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
	chan_cfg.auto_clear = true;
	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_channel, rx_channel));
	const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(22050);
	const i2s_std_config_t *p_i2s_cfg =
		(i2s_config != NULL) ? i2s_config : &std_cfg_default;
	if (tx_channel) {
		ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_channel, p_i2s_cfg));
		ESP_ERROR_CHECK(i2s_channel_enable(*tx_channel));
	}
	if (rx_channel) {
		ESP_ERROR_CHECK(i2s_channel_init_std_mode(*rx_channel, p_i2s_cfg));
		ESP_ERROR_CHECK(i2s_channel_enable(*rx_channel));
	}
	return ESP_OK;
}

static FILE *Music_File = NULL;
static audio_player_callback_event_t expected_event;
static QueueHandle_t event_queue;
static audio_player_callback_event_t event;

static void audio_player_callback(audio_player_cb_ctx_t *ctx) {
	if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
		ESP_LOGI(TAG, "Playback finished");
		s_track_finished = true;
	}
	if (ctx->audio_event == expected_event) {
		xQueueSend(event_queue, &(ctx->audio_event), 0);
	}
}

void Audio_Init(void) {
	i2s_std_config_t std_cfg = {
		.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
		.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
													   I2S_SLOT_MODE_STEREO),
		.gpio_cfg = BSP_I2S_GPIO_CFG,
	};
	esp_err_t ret = bsp_audio_init(&std_cfg, &i2s_tx_chan, &i2s_rx_chan);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to initialize audio: %s", esp_err_to_name(ret));
		return;
	}
	audio_player_config_t config = {.mute_fn = audio_mute_function,
									.write_fn = bsp_i2s_write,
									.clk_set_fn = bsp_i2s_reconfig_clk,
									.priority = 3,
									.coreID = 1};
	ret = audio_player_new(config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create audio player: %s",
				 esp_err_to_name(ret));
		return;
	}
	event_queue = xQueueCreate(1, sizeof(audio_player_callback_event_t));
	if (!event_queue) {
		ESP_LOGE(TAG, "Failed to create event queue");
		return;
	}
	ret = audio_player_callback_register(audio_player_callback, NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to register callback: %s", esp_err_to_name(ret));
		return;
	}
	if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
		ESP_LOGE(TAG, "Expected state to be IDLE"); // The player is not idle
		return;
	}
}

/* ==================== Duree d'un fichier MP3 ==================== */

// Debits (kbit/s) Layer III : [0] MPEG1, [1] MPEG2 / MPEG2.5
static const uint16_t mp3_bitrates[2][15] = {
	{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
	{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
};
static const uint32_t mp3_samplerates[3] = {44100, 48000, 32000};

typedef struct {
	bool mpeg1;
	uint32_t bitrate;	  // bit/s
	uint32_t sample_rate; // Hz
	bool mono;
	uint32_t frame_len; // octets
} mp3_frame_hdr_t;

// Decode un en-tete de trame MPEG Layer III, false s'il est invalide
static bool mp3_parse_header(const uint8_t *h, mp3_frame_hdr_t *out) {
	if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
		return false;
	uint8_t ver = (h[1] >> 3) & 3; // 0 = 2.5, 1 = reserve, 2 = 2, 3 = 1
	uint8_t layer = (h[1] >> 1) & 3;
	uint8_t br_idx = h[2] >> 4;
	uint8_t sr_idx = (h[2] >> 2) & 3;
	if (ver == 1 || layer != 1 || br_idx == 0 || br_idx == 15 || sr_idx == 3)
		return false;
	out->mpeg1 = (ver == 3);
	out->bitrate = mp3_bitrates[out->mpeg1 ? 0 : 1][br_idx] * 1000;
	out->sample_rate = mp3_samplerates[sr_idx] >> (ver == 3	  ? 0
												   : ver == 2 ? 1
															  : 2);
	out->mono = ((h[3] >> 6) == 3);
	out->frame_len = (out->mpeg1 ? 144 : 72) * out->bitrate / out->sample_rate +
					 ((h[2] >> 1) & 1);
	return true;
}

static uint32_t read_be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		   ((uint32_t)p[2] << 8) | p[3];
}

// Estime la duree (s) d'un MP3 : en-tete Xing/Info ou VBRI si present,
// sinon taille des donnees / debit (CBR). Le fichier est rembobine.
static uint32_t mp3_get_duration(FILE *f) {
	uint32_t duration = 0;
	// Tampon sur le tas : la pile de la tache principale (3,5 Ko) est trop
	// petite pour l'y allouer
	const size_t buf_size = 2048;
	uint8_t *buf = malloc(buf_size);
	if (!buf)
		return 0;
	long audio_start = 0;

	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	// Saute le tag ID3v2 (taille "syncsafe" sur 4 x 7 bits)
	if (fread(buf, 1, 10, f) == 10 && memcmp(buf, "ID3", 3) == 0) {
		audio_start = 10 + (((uint32_t)(buf[6] & 0x7F) << 21) |
							((uint32_t)(buf[7] & 0x7F) << 14) |
							((uint32_t)(buf[8] & 0x7F) << 7) | (buf[9] & 0x7F));
		if (buf[5] & 0x10) // pied de tag present
			audio_start += 10;
	}

	// Taille du tag ID3v1 eventuel en fin de fichier
	long audio_end = file_size;
	if (file_size >= 128) {
		fseek(f, file_size - 128, SEEK_SET);
		if (fread(buf, 1, 3, f) == 3 && memcmp(buf, "TAG", 3) == 0)
			audio_end -= 128;
	}

	fseek(f, audio_start, SEEK_SET);
	size_t n = fread(buf, 1, buf_size, f);

	// Cherche la premiere trame valide (confirmee par la trame suivante)
	mp3_frame_hdr_t hdr;
	size_t pos;
	bool found = false;
	for (pos = 0; pos + 4 <= n; pos++) {
		if (!mp3_parse_header(&buf[pos], &hdr))
			continue;
		mp3_frame_hdr_t next;
		size_t next_pos = pos + hdr.frame_len;
		if (next_pos + 4 > n || mp3_parse_header(&buf[next_pos], &next)) {
			found = true;
			break;
		}
	}

	if (found) {
		uint32_t samples_per_frame = hdr.mpeg1 ? 1152 : 576;
		uint32_t side_info =
			hdr.mpeg1 ? (hdr.mono ? 17 : 32) : (hdr.mono ? 9 : 17);
		const uint8_t *xing = &buf[pos + 4 + side_info];
		const uint8_t *vbri = &buf[pos + 4 + 32];
		uint32_t frames = 0;

		if (pos + 4 + side_info + 12 <= n &&
			(memcmp(xing, "Xing", 4) == 0 || memcmp(xing, "Info", 4) == 0) &&
			(read_be32(xing + 4) & 0x1)) {
			frames = read_be32(xing + 8);
		} else if (pos + 4 + 32 + 18 <= n && memcmp(vbri, "VBRI", 4) == 0) {
			frames = read_be32(vbri + 14);
		}

		if (frames) {
			duration = (uint32_t)((uint64_t)frames * samples_per_frame /
								  hdr.sample_rate);
		} else {
			long audio_bytes = audio_end - (audio_start + (long)pos);
			if (audio_bytes > 0)
				duration = (uint32_t)((uint64_t)audio_bytes * 8 / hdr.bitrate);
		}
	}

	free(buf);
	fseek(f, 0, SEEK_SET);
	ESP_LOGI(TAG, "Duree estimee : %lu s", (unsigned long)duration);
	return duration;
}

// Reinitialise le suivi de lecture pour un nouveau morceau
static void Music_track_start(FILE *f) {
	s_duration_sec = mp3_get_duration(f);
	s_frames_played = 0;
	s_track_finished = false;
}

void Play_Music_ex(const char *filePath) {
	Music_pause();
	Music_File = Open_File(filePath);
	if (!Music_File) {
		ESP_LOGE(TAG, "Failed to open MP3 file: %s", filePath);
		return;
	}
	Music_track_start(Music_File);
	expected_event = AUDIO_PLAYER_CALLBACK_EVENT_PLAYING;
	esp_err_t ret = audio_player_play(Music_File);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to play audio: %s", esp_err_to_name(ret));
		fclose(Music_File);
		return;
	}
	if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(100)) != pdPASS) {
		ESP_LOGE(TAG, "Failed to receive playing event");
		fclose(Music_File);
		return;
	}
	if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING) {
		ESP_LOGE(TAG, "Expected state to be PLAYING");
		fclose(Music_File);
		return;
	}
}

void Play_Music(const char *directory, const char *fileName) {
	Music_pause();
	const int maxPathLength = 100;
	char filePath[maxPathLength];
	if (strcmp(directory, "/") == 0) {
		snprintf(filePath, maxPathLength, "%s%s", directory, fileName);
	} else {
		snprintf(filePath, maxPathLength, "%s/%s", directory, fileName);
	}
	Music_File = Open_File(filePath);
	if (!Music_File) {
		ESP_LOGE(TAG, "Failed to open MP3 file: %s", filePath);
		return;
	}
	Music_track_start(Music_File);

	expected_event = AUDIO_PLAYER_CALLBACK_EVENT_PLAYING;
	esp_err_t ret = audio_player_play(Music_File);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to play audio: %s", esp_err_to_name(ret));
		fclose(Music_File);
		return;
	}
	if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(100)) != pdPASS) {
		ESP_LOGE(TAG, "Failed to receive playing event");
		fclose(Music_File);
		return;
	}
	if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING) {
		ESP_LOGE(TAG, "Expected state to be PLAYING");
		fclose(Music_File);
		return;
	}
}

void Music_resume(void) {
	if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING) {
		expected_event = AUDIO_PLAYER_CALLBACK_EVENT_PLAYING;
		esp_err_t ret = audio_player_resume();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to resume audio: %s", esp_err_to_name(ret));
			fclose(Music_File);
			return;
		}
		if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(100)) != pdPASS) {
			ESP_LOGE(TAG, "Failed to receive playing event after resume");
			fclose(Music_File);
			return;
		}
		if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING) {
			ESP_LOGE(TAG, "Expected state to be RESUME");
			fclose(Music_File);
			return;
		}
	}
}
void Music_pause(void) {
	if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
		expected_event = AUDIO_PLAYER_CALLBACK_EVENT_PAUSE;
		esp_err_t ret = audio_player_pause();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to pause audio: %s", esp_err_to_name(ret));
			fclose(Music_File);
			return;
		}
		if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(100)) != pdPASS) {
			ESP_LOGE(TAG, "Failed to receive pause event");
			fclose(Music_File);
			return;
		}
		if (audio_player_get_state() != AUDIO_PLAYER_STATE_PAUSE) {
			ESP_LOGE(TAG, "Expected state to be PAUSE");
			fclose(Music_File);
			return;
		}
	}
}

// Arrête la lecture en cours (le lecteur ferme lui-même le fichier)
void Music_stop(void) {
	if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
		esp_err_t ret = audio_player_stop();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to stop audio: %s", esp_err_to_name(ret));
		}
	}
}

// Duree totale du morceau en cours, en secondes (0 si inconnue)
uint32_t Music_Duration(void) { return s_duration_sec; }

// Temps ecoule depuis le debut du morceau, en secondes
uint32_t Music_Elapsed(void) {
	// En fin de morceau, on renvoie au moins la duree pour que l'UI
	// detecte la fin meme si l'estimation etait un peu trop longue
	uint32_t elapsed = s_sample_rate ? s_frames_played / s_sample_rate : 0;
	if (s_track_finished && elapsed < s_duration_sec)
		return s_duration_sec;
	return elapsed;
}

void Volume_adjustment(uint8_t Vol) {
	if (Vol > Volume_MAX)
		printf(
			"Audio : The volume value is incorrect. Please enter 0 to 21\r\n");
	else
		Volume = Vol;
	ESP_LOGI(TAG, "Volume set to %d", Volume);
}
