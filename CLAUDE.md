# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

BabyBot: an ESP32-S3 child nightlight/alarm clock with a 240×320 ST7789T touch screen (CST328), an I2S DAC (PCM5101) for MP3 playback, an SD card, and a PN532 NFC reader. The UI is built with LVGL 8.3. See README.md for the pin mapping and the SD card layout. Code comments, logs and the README are in French, so write new comments in French too.

## Historique du projet

`doc/HISTORIQUE.md` retrace les demandes et évolutions du projet, jour par jour. **Tiens-le à jour à chaque demande qui change le projet** (fonctionnalité, correctif, refactoring, configuration, organisation des branches) :
- ajoute une puce sous la section du jour (`## JJ/MM : thème`) et crée cette section si elle n'existe pas encore ;
- écris en français, dans le même style que les puces existantes, avec le hash du commit entre parenthèses quand il existe ;
- quand tu commites la modification, inclus la mise à jour de `HISTORIQUE.md` dans le même commit ;
- tiens à jour la section « Points restés ouverts ou à surveiller » : ajoute les problèmes non résolus, retire ceux qui sont réglés.

Les simples questions, explications ou commandes sans effet sur le projet n'y vont pas.

## Build / flash

ESP-IDF, target `esp32s3`. There are no tests and no lint step other than `.clang-format`: LLVM base, **tabs**, width 4.

```bash
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

- `components/` is **git-ignored but required**. It holds local copies of `lvgl__lvgl` (8.3.11), `chmorgan__esp-audio-player` and `chmorgan__esp-libhelix-mp3`. `managed_components/` is fetched automatically (cJSON, `garag/esp-idf-pn532`).
- Outside Windows, the top-level `CMakeLists.txt` switches the dependency lock to `dependencies.linux.lock` (`dependencies.lock` contains Windows paths). Do not merge the two.
- Every new `.c` file must be added to `SRCS` in `main/CMakeLists.txt`, and every new directory to `INCLUDE_DIRS`. Headers are included by bare name (e.g. `#include "PCM5101.h"`).
- `.clangd` hard-codes a GCC toolchain include path under `~/.espressif`. Update it if the toolchain version changes.

## Architecture

`main/` holds one directory per hardware driver (`Audio_Driver`, `LCD_Driver`, `Touch_Driver`, `LVGL_Driver`, `SD_Card`, `BAT_Driver`, `PWR_Key`, `Wireless`, `NFC_Tag`), plus `ota/` for the firmware update. All other application and UI code lives in `main/cute/`.

**Tasks and threading** (`main/main.c`):
- `app_main` initializes SD → LCD → Audio → LVGL → `app_manager_init()` (loads `/sdcard/settings/config.json`), shows the splash screen and plays `startup.mp3`. It then loops forever. Each pass it drains `app_msg_queue`, calls `wakeup()` (the alarm check) and calls `lv_timer_handler()`. **This loop is the only LVGL task, and there is no LVGL mutex.** Do not call `lv_*` from any other task. Send a message on `app_msg_queue` instead.
- `Driver_Loop` (core 0) runs `Wireless_Init()`: it reads Wi-Fi credentials from `/sdcard/settings/wifi.txt` (JSON), then on connection calls `time_sync_start()` (`cute/time_sync.[ch]`: SNTP, Europe/Paris TZ, non-blocking). The SNTP sync callback stops SNTP and posts `MSG_TIME_READY` whenever the time actually arrives. It then polls the battery and power key every 100 ms.
- Every message on `app_msg_queue` goes through `app_manager_handle_msg()`. On `MSG_TIME_READY` it calls `app_manager_start()` and `fw_update_check_start()` (which stops Wi-Fi once done); after `TIME_SYNC_TIMEOUT_MS` (20 s) without the time, the main loop calls `app_manager_start()` itself. That function switches to the robot screen and starts `nfc_task` (core 0), once. Wi-Fi is also stopped by the main loop after `WIFI_GIVE_UP_MS` (5 min) without time; it is used only for the initial time sync and the update check.
- `nfc_task` talks to the PN532 over UART0 in HSU mode (921600 baud, RX 43, TX 44) and puts it in power-down between polls. It calls `app_manager_notify(EVENT_NFC_TAG / EVENT_NFC_TAG_REMOVED)` from its own task; that only posts `MSG_NFC_TAG*` on `app_msg_queue`. The main loop then looks the UID up in `/sdcard/settings/nfc_tags.json` (fallback `/sdcard/<UID hex>.mp3`) and opens the NFC popup (`nfc_popup.c`).

**app_manager** (`cute/app_manager.[ch]`) is the central hub:
- Global state: mood and `alarm_t` (the `days` bitmask uses bit 0 = Monday; `in_settings` suppresses the alarm while it is being edited).
- An event dispatcher (`app_manager_notify`).
- Config persistence to `/sdcard/settings/config.json` via cJSON: the alarm (`alarm` object) and the volume (root `volume`, 0-100, default `VOLUME_DEFAULT`). `set_wakeup_config` rewrites the whole file. The startup sound plays at a fixed volume of 10; the saved volume is applied in `app_manager_start()` and saved when the music player's slider is released (`app_manager_set_volume`).
- Screen navigation via `app_manager_choose_frame(frame_select_t)`.

**Screens ("frames")**: each frame has its own `*_create()` function. That function builds a new `lv_obj_t` screen and calls `lv_scr_load` on it:
- `robot_cute.c` / `robot_face.c`: the robot's face and eyes
- `wakeup_settings.c` / `wakeup_sound.c`: alarm settings
- `wireframe_clock_lvgl.c`: clock
- `mp3_player_lvgl.c`: music player
- `baby_splash_screen.c`: splash screen
- `settings_menu.c`: settings menu (Alarm / Version buttons) and the shared settings screen/button helpers
- `version_screen.c`: firmware version screen

Prototypes for several of these are in `draw_function.h`. Touching anywhere opens the circular menu `pie_dialog_lvgl.c` (`pie_dialog_open`), which calls `app_manager_choose_frame`. To add a screen: add a `FRAME_*` enum value, add a case in `app_manager_choose_frame`, and add an entry in the pie menu.

`main/ressources/*_png.c` files (`robot_png.c`, `horloge_png.c`, …) are generated LVGL image arrays (menu icons, declared in `pie_icons.h`). Do not edit them by hand.

**Firmware update** (`ota/fw_update.[ch]`): on `MSG_TIME_READY`, `fw_update_check_start()` queries the latest GitHub release of `matheopit/BabyBot`. If the `vX.Y.Z` tag is newer than `version.txt`, it downloads the `BabyBot.bin` asset to `/sdcard/update/firmware.bin`, stops Wi-Fi and posts `MSG_FW_READY` (→ `esp_restart`). At boot, if that file exists, `app_main` calls `fw_update_apply_from_sd()` before anything else: it shows a progress screen (`ota/fw_update_lvgl.[ch]`, the only LVGL code of the update), writes to the next OTA partition, deletes the file and restarts. Bootloader rollback is enabled, and `app_manager_start()` marks the app valid. `partitions.csv` has `ota_0`/`ota_1` of 3 MB each. Changing the partitions or the bootloader still requires a USB flash. `./release.sh X.Y.Z` bumps `version.txt`, tags, builds and creates the GitHub release.

**Audio**: `PCM5101.c` wraps `esp-audio-player`: `Play_Music(dir, file)`, `Play_Music_ex(path)`, `Music_stop/pause/resume` and `Volume_adjustment`.
